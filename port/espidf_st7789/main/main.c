/*
 * HTGL ESP-IDF example for the DragonSoul ESP32-S3 box wiring.
 *
 * The renderer owns no framebuffer. Two 240x8 RGB565 bands are alternated:
 * SPI DMA transmits one band while HTGL composes the next one.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "htgl.h"
#include "htgl_internal.h"

extern const unsigned char htgl_espidf_blob[];
extern const unsigned int htgl_espidf_blob_len;

enum {
    DISPLAY_WIDTH = 240,
    DISPLAY_HEIGHT = 280,
    BAND_ROWS = 8,
    LINE_BUFFER_COUNT = 2,
    FRAME_PERIOD_MS = 33,
    UI_TASK_STACK_BYTES = 8 * 1024,
    UI_TASK_CORE = 1,
};

/* Exact display wiring on the ESP32-S3 box (SPI3_HOST, write-only ST7789). */
enum {
    PIN_DISP_EN = 17,  /* active-low LCD rail */
    PIN_DISP_BL = 45,
    PIN_DISP_DC = 38,
    PIN_DISP_CS = 39,
    PIN_DISP_SCLK = 40,
    PIN_DISP_MOSI = 41,
    PIN_DISP_RST = 47,
};

static const char *const TAG = "htgl-espidf";
static uint16_t line_buffers[LINE_BUFFER_COUNT][DISPLAY_WIDTH * BAND_ROWS];
static htgl_ctx htgl;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t color_done;
static unsigned queued_transfers;

static void fatal(const char *message)
{
    ESP_LOGE(TAG, "%s", message);
    abort();
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *event,
                                void *user_context)
{
    (void)io;
    (void)event;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_context, &task_woken);
    return task_woken == pdTRUE;
}

static void wait_for_color_transfer(void)
{
    if (queued_transfers == 0) return;
    if (xSemaphoreTake(color_done, pdMS_TO_TICKS(100)) != pdTRUE) {
        fatal("ST7789 DMA transfer timed out");
    }
    --queued_transfers;
}

static void wait_for_display_idle(void)
{
    while (queued_transfers != 0) wait_for_color_transfer();
}

static int other_line_buffer(const uint16_t *pixels)
{
    if (pixels == line_buffers[0]) return 1;
    if (pixels == line_buffers[1]) return 0;
    fatal("HTGL submitted an unknown line buffer");
    return 0;
}

static void flush(int x, int y, int width, int height, const uint16_t *pixels)
{
    /* The panel expects big-endian RGB565; HTGL renders native-endian words. */
    uint16_t *mutable_pixels = (uint16_t *)pixels;
    for (int index = 0; index < width * height; ++index) {
        const uint16_t color = mutable_pixels[index];
        mutable_pixels[index] = (uint16_t)((color << 8) | (color >> 8));
    }

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x, y, x + width, y + height,
                                               mutable_pixels));
    ++queued_transfers;

    /* Never give HTGL a DMA-owned buffer. Waiting here releases the oldest
       transfer only when both buffers are occupied, creating a two-stage pipe. */
    if (queued_transfers == LINE_BUFFER_COUNT) wait_for_color_transfer();
    htgl.line_buf = line_buffers[other_line_buffer(pixels)];
}

static void init_display(void)
{
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << PIN_DISP_EN) | (1ULL << PIN_DISP_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));
    gpio_set_level(PIN_DISP_EN, 0);
    gpio_set_level(PIN_DISP_BL, 0);

    const spi_bus_config_t bus = {
        .mosi_io_num = PIN_DISP_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = PIN_DISP_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = DISPLAY_WIDTH * BAND_ROWS * (int)sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

    color_done = xSemaphoreCreateBinary();
    if (color_done == NULL) fatal("could not allocate DMA completion semaphore");

    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = PIN_DISP_CS,
        .dc_gpio_num = PIN_DISP_DC,
        .spi_mode = 0,
        .pclk_hz = 80 * 1000 * 1000,
        .trans_queue_depth = LINE_BUFFER_COUNT,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t panel_io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &panel_io));

    const esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(panel_io, &callbacks, color_done));

    const esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = PIN_DISP_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 20));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    gpio_set_level(PIN_DISP_BL, 1);
}

static void htgl_task(void *argument)
{
    (void)argument;
    init_display();

    static const htgl_hal hal = { .flush = flush };
    htgl_init(&htgl, &hal, line_buffers[0], DISPLAY_WIDTH * BAND_ROWS);
    const int load_result = htgl_load(&htgl, htgl_espidf_blob, (int)htgl_espidf_blob_len);
    if (load_result != 0) fatal("htgl_load failed");

    ESP_LOGI(TAG, "HTGL ready: %dx%d, blob=%u bytes", htgl_screen_w(&htgl),
             htgl_screen_h(&htgl), htgl_espidf_blob_len);
    htgl_layout(&htgl);
    htgl_render(&htgl);
    wait_for_display_idle();

    TickType_t next_frame = xTaskGetTickCount();
    for (;;) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (htgl_tick(&htgl, now_ms) != 0) {
            htgl_layout(&htgl);
            htgl_render(&htgl);
            wait_for_display_idle();
        }
        vTaskDelayUntil(&next_frame, pdMS_TO_TICKS(FRAME_PERIOD_MS));
    }
}

void app_main(void)
{
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        htgl_task, "htgl-ui", UI_TASK_STACK_BYTES, NULL, 5, NULL, UI_TASK_CORE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) fatal("could not create HTGL UI task in PSRAM");
}
