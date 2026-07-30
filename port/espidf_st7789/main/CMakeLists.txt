idf_component_register(
    SRCS
        "main.c"
        "ui_blob.c"
        "../../../engine/htgl.c"
        "../../../engine/draw.c"
    INCLUDE_DIRS "../../../engine"
    REQUIRES esp_lcd driver
)
