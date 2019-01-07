#ifndef _EPD_UI_H_
#define _EPD_UI_H_

typedef struct ui_data {
    int partial_display_cnt;
    char message[128*3+1];
} ui_data_t;

extern ui_data_t ui_data;

int esp_ui_init();
void esp_ui_update();

#endif