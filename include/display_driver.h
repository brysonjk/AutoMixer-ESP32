#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <lvgl.h>
#include <Arduino.h>

// ESP32-8048S070 Pin definitions
#define TFT_BL 2
#define TFT_DE 41
#define TFT_VSYNC 40
#define TFT_HSYNC 39
#define TFT_PCLK 42
#define TFT_R0 14
#define TFT_R1 21
#define TFT_R2 47
#define TFT_R3 48
#define TFT_R4 45
#define TFT_G0 9
#define TFT_G1 46
#define TFT_G2 3
#define TFT_G3 8
#define TFT_G4 16
#define TFT_G5 1
#define TFT_B0 15
#define TFT_B1 7
#define TFT_B2 6
#define TFT_B3 5
#define TFT_B4 4

// Touch pins (GT911)
#define TOUCH_SDA 19
#define TOUCH_SCL 20
#define TOUCH_INT 18
#define TOUCH_RST 38
#define TOUCH_WIDTH 800
#define TOUCH_HEIGHT 480

// Display dimensions
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

class DisplayDriver {
public:
    static bool init();
    static void setBrightness(uint8_t brightness);

private:
    static void flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
    static void touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
};

#endif
