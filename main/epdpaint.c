#include "epdpaint.h"
#include "epd2in9.h"
#include "utf8_gb2312.h"
#include <string.h>


int epdpaint_display_rotate = ROTATE_270;
unsigned char* epdpaint_frame_buffer = 0;

void epdpaint_draw_absolute_pixel(int x, int y, int colored) {
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    if (IF_INVERT_COLOR) {
        if (colored) {
            epdpaint_frame_buffer[(x + y * EPD_WIDTH) / 8] |= 0x80 >> (x % 8); //set bit
        } else {
            epdpaint_frame_buffer[(x + y * EPD_WIDTH) / 8] &= ~(0x80 >> (x % 8)); //clear bit
        }
    } else {
        if (colored) {
            epdpaint_frame_buffer[(x + y * EPD_WIDTH) / 8] &= ~(0x80 >> (x % 8)); //clear bit
        } else {
            epdpaint_frame_buffer[(x + y * EPD_WIDTH) / 8] |= 0x80 >> (x % 8); //set bit
        }
    }
}

void epdpaint_clear(int colored) {
    for (int x = 0; x < EPD_WIDTH; x++) {
        for (int y = 0; y < EPD_HEIGHT; y++) {
            epdpaint_draw_absolute_pixel(x, y, colored);
        }
    }
}

void epdpaint_draw_pixel(int x, int y, int colored) {
    int point_temp;
    if (epdpaint_display_rotate == ROTATE_0) {
        if(x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
            return;
        }
        epdpaint_draw_absolute_pixel(x, y, colored);
    } else if (epdpaint_display_rotate == ROTATE_90) {
        if(x < 0 || x >= EPD_HEIGHT || y < 0 || y >= EPD_WIDTH) {
          return;
        }
        point_temp = x;
        x = EPD_WIDTH - y;
        y = point_temp;
        epdpaint_draw_absolute_pixel(x, y, colored);
    } else if (epdpaint_display_rotate == ROTATE_180) {
        if(x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
          return;
        }
        x = EPD_WIDTH - x;
        y = EPD_HEIGHT - y;
        epdpaint_draw_absolute_pixel(x, y, colored);
    } else if (epdpaint_display_rotate == ROTATE_270) {
        if(x < 0 || x >= EPD_HEIGHT || y < 0 || y >= EPD_WIDTH) {
          return;
        }
        point_temp = x;
        x = y;
        y = EPD_HEIGHT - point_temp;
        epdpaint_draw_absolute_pixel(x, y, colored);
    }
}

void epdpaint_draw_ascii_char(int x, int y, char ascii_char, epd_font* font, int colored) {
    unsigned int char_offset = (ascii_char - ' ') * font->height * (font->width / 8 + (font->width % 8 ? 1 : 0));
    const unsigned char* ptr = &font->table[char_offset];

    for (int j = 0; j < font->height; j++) {
        for (int i = 0; i < font->width; i++) {
            if (*ptr & (0x80 >> (i % 8))) {
                epdpaint_draw_pixel(x + i, y + j, colored);
            }
            if (i % 8 == 7) {
                ptr++;
            }
        }
        if (font->width % 8 != 0) {
            ptr++;
        }
    }
}

void epdpaint_draw_gb2312_char(int x, int y, unsigned short gb2312_char, epd_font* font, int colored) {
    int gb2312_row = (gb2312_char >> 8) - 0xA0;
    int gb2312_col = (gb2312_char & 0xFF) - 0xA0;
    unsigned int char_offset = (94 * (gb2312_row - 1) + (gb2312_col - 1)) * 32;

    unsigned char buffer[(font->width/8) * font->height];
    fseek(font->file, char_offset, SEEK_SET);
    fread(buffer, sizeof(buffer), 1, font->file);

    const unsigned char* ptr = buffer;
    for (int j = 0; j < font->height; j++) {
        for (int i = 0; i < font->width; i++) {
            if (*ptr & (0x80 >> (i % 8))) {
                epdpaint_draw_pixel(x + i, y + j, colored);
            }
            if (i % 8 == 7) {
                ptr++;
            }
        }
        if (font->width % 8 != 0) {
            ptr++;
        }
    }
}

void epdpaint_draw_utf8_string(int x, int y, const char* text, epd_font* en_font, epd_font* zh_font, int colored) {
    const char* p_text = text;
    int refcolumn = x;
    while (*p_text != 0) {
        if ((*p_text & 0x80) == 0) {  // ascii
            epdpaint_draw_ascii_char(refcolumn, y, *p_text, en_font, colored);
            refcolumn += en_font->width;
            p_text++;
        } else if ((*p_text & 0xE0) == 0xE0) {  // chinese
            epdpaint_draw_gb2312_char(refcolumn, y, utf8_to_gb2312(p_text), zh_font, colored);
            refcolumn += zh_font->width;
            p_text+=3;
        } else {
            p_text++;
        }

    }
}

void epdpaint_draw_line(int x0, int y0, int x1, int y1, int colored) {
    /* Bresenham algorithm */
    int dx = x1 - x0 >= 0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0 <= 0 ? y1 - y0 : y0 - y1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while((x0 != x1) && (y0 != y1)) {
        epdpaint_draw_pixel(x0, y0 , colored);
        if (2 * err >= dy) {     
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx) {
            err += dx; 
            y0 += sy;
        }
    }
}

void epdpaint_draw_horizontal_line(int x, int y, int width, int colored) {
    for (int i = x; i < x + width; i++) {
        epdpaint_draw_pixel(i, y, colored);
    }
}

void epdpaint_draw_vertical_line(int x, int y, int height, int colored) {
    for (int i = y; i < y + height; i++) {
        epdpaint_draw_pixel(x, i, colored);
    }
}

void epdpaint_draw_rectangle(int x0, int y0, int x1, int y1, int colored) {
    int min_x = x1 > x0 ? x0 : x1;
    int max_x = x1 > x0 ? x1 : x0;
    int min_y = y1 > y0 ? y0 : y1;
    int max_y = y1 > y0 ? y1 : y0;

    epdpaint_draw_horizontal_line(min_x, min_y, max_x - min_x + 1, colored);
    epdpaint_draw_horizontal_line(min_x, max_y, max_x - min_x + 1, colored);
    epdpaint_draw_vertical_line(min_x, min_y, max_y - min_y + 1, colored);
    epdpaint_draw_vertical_line(max_x, min_y, max_y - min_y + 1, colored);
}

void epdpaint_draw_filled_rectangle(int x0, int y0, int x1, int y1, int colored) {
    int min_x = x1 > x0 ? x0 : x1;
    int max_x = x1 > x0 ? x1 : x0;
    int min_y = y1 > y0 ? y0 : y1;
    int max_y = y1 > y0 ? y1 : y0;
    
    for (int i = min_x; i <= max_x; i++) {
      epdpaint_draw_vertical_line(i, min_y, max_y - min_y + 1, colored);
    }
}

void epdpaint_draw_circle(int x, int y, int radius, int colored) {
    /* Bresenham algorithm */
    int x_pos = -radius;
    int y_pos = 0;
    int err = 2 - 2 * radius;
    int e2;

    do {
        epdpaint_draw_pixel(x - x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(x + x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(x + x_pos, y - y_pos, colored);
        epdpaint_draw_pixel(x - x_pos, y - y_pos, colored);
        e2 = err;
        if (e2 <= y_pos) {
            err += ++y_pos * 2 + 1;
            if(-x_pos == y_pos && e2 <= x_pos) {
              e2 = 0;
            }
        }
        if (e2 > x_pos) {
            err += ++x_pos * 2 + 1;
        }
    } while (x_pos <= 0);
}

void epdpaint_draw_filled_circle(int x, int y, int radius, int colored) {
    /* Bresenham algorithm */
    int x_pos = -radius;
    int y_pos = 0;
    int err = 2 - 2 * radius;
    int e2;

    do {
        epdpaint_draw_pixel(x - x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(x + x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(x + x_pos, y - y_pos, colored);
        epdpaint_draw_pixel(x - x_pos, y - y_pos, colored);
        epdpaint_draw_horizontal_line(x + x_pos, y + y_pos, 2 * (-x_pos) + 1, colored);
        epdpaint_draw_horizontal_line(x + x_pos, y - y_pos, 2 * (-x_pos) + 1, colored);
        e2 = err;
        if (e2 <= y_pos) {
            err += ++y_pos * 2 + 1;
            if(-x_pos == y_pos && e2 <= x_pos) {
                e2 = 0;
            }
        }
        if(e2 > x_pos) {
            err += ++x_pos * 2 + 1;
        }
    } while(x_pos <= 0);
}
