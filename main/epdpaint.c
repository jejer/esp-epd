#include "epdpaint.h"

#include "epd2in9.h"
#include "utf8_gb2312.h"

#include "esp_log.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "EPD-PAINT";


esp_painter_handle_t epdpaint_init(int rotate, int x, int y, int width, int height) {
    esp_painter_handle_t painter = malloc(sizeof(esp_painter_t));
    if (!painter) {
        ESP_LOGE(TAG, "no memory for painter");
        return 0;
    }

    painter->rotate = rotate;
    switch (rotate) {
        case ROTATE_0:
            painter->abs_width = width % 8 ? width + 8 - (width % 8) : width;
            painter->abs_height = height;
            painter->abs_x = x;
            painter->abs_y = y;
            break;
        case ROTATE_90:
            painter->abs_width = height % 8 ? height + 8 - (height % 8) : height;
            painter->abs_height = width;
            painter->abs_x = EPD_WIDTH - y - painter->abs_width;
            painter->abs_y = x;
            break;
        case ROTATE_180:
            painter->abs_width = width % 8 ? width + 8 - (width % 8) : width;
            painter->abs_height = height;
            painter->abs_x = EPD_WIDTH - x - painter->abs_width;
            painter->abs_y = EPD_HEIGHT - y - painter->abs_height;
            break;
        case ROTATE_270:
            painter->abs_width = height % 8 ? height + 8 - (height % 8) : height;
            painter->abs_height = width;
            painter->abs_x = y;
            painter->abs_y = EPD_HEIGHT - x - painter->abs_height;
            break;
        default:
            ESP_LOGE(TAG, "not a valid rotate(%d)", rotate);
            break;
    }

    painter->buffer = heap_caps_malloc((painter->abs_width/8) * painter->abs_height, MALLOC_CAP_DMA);
    if (!painter->buffer) {
        ESP_LOGE(TAG, "no memory for paint buffer");
        free(painter);
        return 0;
    }
    return painter;
}

void epdpaint_destroy(esp_painter_handle_t painter) {
    free(painter->buffer);
    free(painter);
}

void epdpaint_draw_absolute_pixel(esp_painter_handle_t painter, int x, int y, int colored) {
    if (x < 0 || x >= painter->abs_width || y < 0 || y >= painter->abs_height) {
        return;
    }
    if (IF_INVERT_COLOR) {
        if (colored) {
            painter->buffer[(x + y * painter->abs_width) / 8] |= 0x80 >> (x % 8); //set bit
        } else {
            painter->buffer[(x + y * painter->abs_width) / 8] &= ~(0x80 >> (x % 8)); //clear bit
        }
    } else {
        if (colored) {
            painter->buffer[(x + y * painter->abs_width) / 8] &= ~(0x80 >> (x % 8)); //clear bit
        } else {
            painter->buffer[(x + y * painter->abs_width) / 8] |= 0x80 >> (x % 8); //set bit
        }
    }
}

void epdpaint_clear(esp_painter_handle_t painter, int colored) {
    for (int x = 0; x < painter->abs_width; x++) {
        for (int y = 0; y < painter->abs_height; y++) {
            epdpaint_draw_absolute_pixel(painter, x, y, colored);
        }
    }
}

void epdpaint_draw_pixel(esp_painter_handle_t painter, int x, int y, int colored) {
    int point_temp;
    if (painter->rotate == ROTATE_0) {
        if(x < 0 || x >= painter->abs_width || y < 0 || y >= painter->abs_height) {
            return;
        }
        epdpaint_draw_absolute_pixel(painter, x, y, colored);
    } else if (painter->rotate == ROTATE_90) {
        if(x < 0 || x >= painter->abs_height || y < 0 || y >= painter->abs_width) {
          return;
        }
        point_temp = x;
        x = painter->abs_width - y;
        y = point_temp;
        epdpaint_draw_absolute_pixel(painter, x, y, colored);
    } else if (painter->rotate == ROTATE_180) {
        if(x < 0 || x >= painter->abs_width || y < 0 || y >= painter->abs_height) {
          return;
        }
        x = painter->abs_width - x;
        y = painter->abs_height - y;
        epdpaint_draw_absolute_pixel(painter, x, y, colored);
    } else if (painter->rotate == ROTATE_270) {
        if(x < 0 || x >= painter->abs_height || y < 0 || y >= painter->abs_width) {
          return;
        }
        point_temp = x;
        x = y;
        y = painter->abs_height - point_temp;
        epdpaint_draw_absolute_pixel(painter, x, y, colored);
    }
}

void epdpaint_draw_asc_char(esp_painter_handle_t painter, int x, int y, char asc_char, epd_font_t* font, int colored) {
    unsigned int char_offset = (asc_char - ' ') * font->height * (font->width / 8 + (font->width % 8 ? 1 : 0));
    const unsigned char* ptr = &font->table[char_offset];

    for (int j = 0; j < font->height; j++) {
        for (int i = 0; i < font->width; i++) {
            if (*ptr & (0x80 >> (i % 8))) {
                epdpaint_draw_pixel(painter, x + i, y + j, colored);
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

void epdpaint_draw_gb2312_char(esp_painter_handle_t painter, int x, int y, uint16_t gb2312_char, epd_font_t* font, int colored) {
    int gb2312_row = (gb2312_char >> 8) - 0xA0;
    int gb2312_col = (gb2312_char & 0xFF) - 0xA0;
    uint8_t buffer[(font->width/8) * font->height];
    unsigned int char_offset = (94 * (gb2312_row - 1) + (gb2312_col - 1)) * sizeof(buffer);
    if (gb2312_char == 0) {
        char_offset = 0;
    }

    fseek(font->file, char_offset, SEEK_SET);
    fread(buffer, sizeof(buffer), 1, font->file);

    const uint8_t* ptr = buffer;
    for (int j = 0; j < font->height; j++) {
        for (int i = 0; i < font->width; i++) {
            if (*ptr & (0x80 >> (i % 8))) {
                epdpaint_draw_pixel(painter, x + i, y + j, colored);
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

void epdpaint_draw_utf8_string(esp_painter_handle_t painter, int x, int y, int width, int height, const char* text, epd_font_t* en_font, epd_font_t* zh_font, int colored) {
    const char* p_text = text;
    int x_offset = x;
    int y_offset = y;
    int x_painted = 0;
    int y_painted = 0;
    while (*p_text != 0) {
        // line wrap
        if (x_painted + (zh_font?zh_font->width:en_font->width) > width) {
            x_painted = 0;
            x_offset = x;
            y_painted += zh_font?zh_font->height:en_font->height;
            y_offset += zh_font?zh_font->height:en_font->height;
        }
        // out of height
        if (y_painted + (zh_font?zh_font->height:en_font->height) > height) {
            return;
        }
        if ((*p_text & 0x80) == 0) {  // ascii
            epdpaint_draw_asc_char(painter, x_offset, y_offset, *p_text, en_font, colored);
            x_offset += en_font->width;
            x_painted += en_font->width;
            p_text++;
        } else if ((*p_text & 0xE0) == 0xE0) {  // chinese
            epdpaint_draw_gb2312_char(painter, x_offset, y_offset, utf8_to_gb2312(p_text), zh_font, colored);
            x_offset += zh_font->width;
            x_painted += zh_font->width;
            p_text+=3;
        } else {
            p_text++;
        }
    }
}

void epdpaint_draw_img(esp_painter_handle_t painter, int x, int y, int width, int height, const uint8_t *img, int colored) {
    const uint8_t* ptr = img;

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            if (*ptr & (0x80 >> (i % 8))) {
                epdpaint_draw_pixel(painter, x + i, y + j, colored);
            }
            if (i % 8 == 7) {
                ptr++;
            }
        }
        if (width % 8 != 0) {
            ptr++;
        }
    }
}

void epdpaint_draw_line(esp_painter_handle_t painter, int x0, int y0, int x1, int y1, int colored) {
    /* Bresenham algorithm */
    int dx = x1 - x0 >= 0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0 <= 0 ? y1 - y0 : y0 - y1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while((x0 != x1) && (y0 != y1)) {
        epdpaint_draw_pixel(painter, x0, y0 , colored);
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

void epdpaint_draw_horizontal_line(esp_painter_handle_t painter, int x, int y, int width, int colored) {
    for (int i = x; i < x + width; i++) {
        epdpaint_draw_pixel(painter, i, y, colored);
    }
}

void epdpaint_draw_vertical_line(esp_painter_handle_t painter, int x, int y, int height, int colored) {
    for (int i = y; i < y + height; i++) {
        epdpaint_draw_pixel(painter, x, i, colored);
    }
}

void epdpaint_draw_rectangle(esp_painter_handle_t painter, int x0, int y0, int x1, int y1, int colored) {
    int min_x = x1 > x0 ? x0 : x1;
    int max_x = x1 > x0 ? x1 : x0;
    int min_y = y1 > y0 ? y0 : y1;
    int max_y = y1 > y0 ? y1 : y0;

    epdpaint_draw_horizontal_line(painter, min_x, min_y, max_x - min_x + 1, colored);
    epdpaint_draw_horizontal_line(painter, min_x, max_y, max_x - min_x + 1, colored);
    epdpaint_draw_vertical_line(painter, min_x, min_y, max_y - min_y + 1, colored);
    epdpaint_draw_vertical_line(painter, max_x, min_y, max_y - min_y + 1, colored);
}

void epdpaint_draw_filled_rectangle(esp_painter_handle_t painter, int x0, int y0, int x1, int y1, int colored) {
    int min_x = x1 > x0 ? x0 : x1;
    int max_x = x1 > x0 ? x1 : x0;
    int min_y = y1 > y0 ? y0 : y1;
    int max_y = y1 > y0 ? y1 : y0;

    for (int i = min_x; i <= max_x; i++) {
      epdpaint_draw_vertical_line(painter, i, min_y, max_y - min_y + 1, colored);
    }
}

void epdpaint_draw_circle(esp_painter_handle_t painter, int x, int y, int radius, int colored) {
    /* Bresenham algorithm */
    int x_pos = -radius;
    int y_pos = 0;
    int err = 2 - 2 * radius;
    int e2;

    do {
        epdpaint_draw_pixel(painter, x - x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(painter, x + x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(painter, x + x_pos, y - y_pos, colored);
        epdpaint_draw_pixel(painter, x - x_pos, y - y_pos, colored);
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

void epdpaint_draw_filled_circle(esp_painter_handle_t painter, int x, int y, int radius, int colored) {
    /* Bresenham algorithm */
    int x_pos = -radius;
    int y_pos = 0;
    int err = 2 - 2 * radius;
    int e2;

    do {
        epdpaint_draw_pixel(painter, x - x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(painter, x + x_pos, y + y_pos, colored);
        epdpaint_draw_pixel(painter, x + x_pos, y - y_pos, colored);
        epdpaint_draw_pixel(painter, x - x_pos, y - y_pos, colored);
        epdpaint_draw_horizontal_line(painter, x + x_pos, y + y_pos, 2 * (-x_pos) + 1, colored);
        epdpaint_draw_horizontal_line(painter, x + x_pos, y - y_pos, 2 * (-x_pos) + 1, colored);
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
