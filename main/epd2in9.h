// https://www.waveshare.com/wiki/2.9inch_e-Paper_Module

#ifndef _EPD2IN9_H_
#define _EPD2IN9_H_

#include <stdint.h>

// Display resolution
#define EPD_WIDTH       128
#define EPD_HEIGHT      296

#define EPD_2IN9_LUT_UPDATE_FULL 0
#define EPD_2IN9_LUT_UPDATE_PART 1

void epd_init(int lut_update_mode);
void epd_sleep();
void epd_set_image_memory(const uint8_t* image_buffer, int x, int y, int width, int height);
void epd_set_frame_memory(const uint8_t* frame_buffer);
void epd_display_frame();

#endif /* _EPD2IN9_H_ */

/* END OF FILE */
