#ifndef HAL_PNG_H
#define HAL_PNG_H

#include <stdint.h>
#include "htgl.h"

/* Allocate an image of w*h and return a HAL whose flush() writes into it. */
void hal_png_begin(int w, int h);
htgl_hal hal_png_get(void);
int hal_png_write(const char *path);   /* 0 on success */
void hal_png_end(void);

#endif
