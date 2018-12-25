#ifndef EPDFONT_H
#define EPDFONT_H

#include <stdio.h>

typedef struct epd_font_t
{
    int width;
    int height;
    FILE* file;
    const unsigned char* table;
} epd_font;

extern epd_font epd_font_ascii_16;

#endif