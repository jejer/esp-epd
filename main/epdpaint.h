#ifndef _EPDPAINT_H_
#define _EPDPAINT_H_

#include "epdfont.h"
#include <stdint.h>

// Display orientation
#define ROTATE_0            0
#define ROTATE_90           1
#define ROTATE_180          2
#define ROTATE_270          3

// Color inverse. 1 or 0 = set or reset a bit if set a colored pixel
#define IF_INVERT_COLOR     0

#define WHITE               0
#define BLACK               1

typedef struct esp_painter
{
    uint8_t *buffer;
    uint8_t rotate;
    int abs_x;
    int abs_y;
    int abs_width;
    int abs_height;
} esp_painter_t;
typedef esp_painter_t* esp_painter_handle_t;

esp_painter_handle_t epdpaint_init(int rotate, int x, int y, int width, int height);
void epdpaint_destroy(esp_painter_handle_t painter);
void epdpaint_draw_absolute_pixel(esp_painter_handle_t painter, int x, int y, int colored);
void epdpaint_clear(esp_painter_handle_t painter, int colored);
void epdpaint_draw_pixel(esp_painter_handle_t painter, int x, int y, int colored);
void epdpaint_draw_line(esp_painter_handle_t painter, int x0, int y0, int x1, int y1, int colored);
void epdpaint_draw_horizontal_line(esp_painter_handle_t painter, int x, int y, int width, int colored);
void epdpaint_draw_vertical_line(esp_painter_handle_t painter, int x, int y, int height, int colored);
void epdpaint_draw_rectangle(esp_painter_handle_t painter, int x0, int y0, int x1, int y1, int colored);
void epdpaint_draw_filled_rectangle(esp_painter_handle_t painter, int x0, int y0, int x1, int y1, int colored);
void epdpaint_draw_circle(esp_painter_handle_t painter, int x, int y, int radius, int colored);
void epdpaint_draw_filled_circle(esp_painter_handle_t painter, int x, int y, int radius, int colored);

void epdpaint_draw_asc_char(esp_painter_handle_t painter, int x, int y, char asc_char, epd_font_t* font, int colored);
void epdpaint_draw_gb2312_char(esp_painter_handle_t painter, int x, int y, uint16_t gb2312_char, epd_font_t* font, int colored);
void epdpaint_draw_utf8_string(esp_painter_handle_t painter, int x, int y, int width, int height, const char* text, epd_font_t* en_font, epd_font_t* zh_font, int colored);
void epdpaint_draw_img(esp_painter_handle_t painter, int x, int y, int width, int height, const uint8_t *img, int colored);

#endif