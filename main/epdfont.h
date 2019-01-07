#ifndef _EPDFONT_H_
#define _EPDFONT_H_

#include <stdint.h>
#include <stdio.h>

typedef struct epd_font
{
    int width;
    int height;
    FILE* file;
    const uint8_t* table;
} epd_font_t;

extern epd_font_t epd_font_asc_8;
extern epd_font_t epd_font_asc_12;
extern epd_font_t epd_font_asc_16;
/*
extern epd_font_t epd_font_asc_20;
extern epd_font_t epd_font_asc_24;
*/

#endif