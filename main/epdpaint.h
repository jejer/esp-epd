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

void epdpaint_init(uint8_t* frame_buffer, int rotate);
void epdpaint_draw_absolute_pixel(int x, int y, int colored);
void epdpaint_clear(int colored);
void epdpaint_draw_pixel(int x, int y, int colored);
void epdpaint_draw_line(int x0, int y0, int x1, int y1, int colored);
void epdpaint_draw_horizontal_line(int x, int y, int width, int colored);
void epdpaint_draw_vertical_line(int x, int y, int height, int colored);
void epdpaint_draw_rectangle(int x0, int y0, int x1, int y1, int colored);
void epdpaint_draw_filled_rectangle(int x0, int y0, int x1, int y1, int colored);
void epdpaint_draw_circle(int x, int y, int radius, int colored);
void epdpaint_draw_filled_circle(int x, int y, int radius, int colored);

void epdpaint_draw_asc_char(int x, int y, char asc_char, epd_font_t* font, int colored);
void epdpaint_draw_gb2312_char(int x, int y, uint16_t gb2312_char, epd_font_t* font, int colored);
void epdpaint_draw_utf8_string(int x, int y, int width, int height, const char* text, epd_font_t* en_font, epd_font_t* zh_font, int colored);

#endif