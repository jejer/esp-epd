#ifndef EPDFONT_H
#define EPDFONT_H

#include <stdint.h>
#include <stdio.h>

typedef struct epd_font
{
    int width;
    int height;
    FILE* file;
    const uint8_t* table;
} epd_font_t;

extern epd_font_t epd_font_ascii_16;

#endif