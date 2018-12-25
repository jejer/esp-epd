#ifndef EPDPAINT_H
#define EPDPAINT_H

#include "epdfont.h"

// Display orientation
#define ROTATE_0            0
#define ROTATE_90           1
#define ROTATE_180          2
#define ROTATE_270          3

// Color inverse. 1 or 0 = set or reset a bit if set a colored pixel
#define IF_INVERT_COLOR     0

extern int epdpaint_display_rotate;
extern unsigned char* epdpaint_frame_buffer;

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

void epdpaint_draw_ascii_char(int x, int y, char ascii_char, epd_font* font, int colored);
void epdpaint_draw_gb2312_char(int x, int y, unsigned short gb2312_char, epd_font* font, int colored);
void epdpaint_draw_utf8_string(int x, int y, const char* text, epd_font* en_font, epd_font* zh_font, int colored);

#endif