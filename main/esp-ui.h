#ifndef _EPD_UI_H_
#define _EPD_UI_H_

typedef struct ui_data {
    int refresh_counter;
    char message[128*3+1];
} ui_data_t;

extern ui_data_t ui_data;

int esp_ui_init();
void esp_ui_full_paint();
void esp_ui_paint_time();

#endif