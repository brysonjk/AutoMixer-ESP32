#include "display_driver.h"
#include "touch.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static esp_lcd_panel_handle_t panel_handle = NULL;
static TouchGT911 touch;

// Internal RAM budget: bounce buffers (64 KB), this draw buffer, and the WiFi driver
// all need it. It only fits because LVGL's object heap lives in PSRAM (see lv_conf.h).
static const int BUF_LINES = 40;

bool DisplayDriver::init() {
    Serial.printf("PSRAM size: %lu bytes\n", (unsigned long)ESP.getPsramSize());
    Serial.printf("Free PSRAM:  %lu bytes\n", (unsigned long)ESP.getFreePsram());
    Serial.printf("Free heap:   %lu bytes\n", (unsigned long)ESP.getFreeHeap());

    esp_lcd_rgb_panel_config_t panel_config;
    memset(&panel_config, 0, sizeof(panel_config));

    panel_config.clk_src = LCD_CLK_SRC_PLL160M;
    // Timings are the panel datasheet's typical values (JC8048B070N): DCLK 29-38 MHz,
    // horizontal blank 226-286 clocks, thpw + thb fixed at 46, tvpw + tvb fixed at 23.
    // DPI panel with no framebuffer of its own, so pclk sets the refresh rate directly:
    // px/frame = 1056 * 525, and 32 MHz (an integer divide of the 160 MHz source) gives
    // 57.7 Hz. Below ~40 Hz it visibly flickers.
    panel_config.timings.pclk_hz = 32000000;
    panel_config.timings.h_res = SCREEN_WIDTH;
    panel_config.timings.v_res = SCREEN_HEIGHT;
    panel_config.timings.hsync_pulse_width = 30;
    panel_config.timings.hsync_back_porch = 16;
    panel_config.timings.hsync_front_porch = 210;
    panel_config.timings.vsync_pulse_width = 13;
    panel_config.timings.vsync_back_porch = 10;
    panel_config.timings.vsync_front_porch = 22;
    panel_config.timings.flags.pclk_active_neg = 1;

    panel_config.data_width = 16;
    panel_config.dma_burst_size = 64;

    // Two internal-RAM bounce buffers the DMA feeds from, refilled from the PSRAM
    // framebuffer in bursts. Without this the DMA stalls whenever the CPU touches
    // flash or PSRAM (they share the bus arbiter) and the image walks diagonally.
    // 20 lines of slack. WiFi and full-screen redraws (switching screens) burst hard
    // enough that a smaller buffer underruns, and an underrun leaves the panel
    // permanently out of sync rather than glitching for one frame.
    panel_config.bounce_buffer_size_px = SCREEN_WIDTH * 20;
    panel_config.hsync_gpio_num = TFT_HSYNC;
    panel_config.vsync_gpio_num = TFT_VSYNC;
    panel_config.de_gpio_num = TFT_DE;
    panel_config.pclk_gpio_num = TFT_PCLK;
    panel_config.disp_gpio_num = -1;

    const int data_pins[16] = {
        TFT_B0, TFT_B1, TFT_B2, TFT_B3, TFT_B4,
        TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
        TFT_R0, TFT_R1, TFT_R2, TFT_R3, TFT_R4,
    };
    memcpy(panel_config.data_gpio_nums, data_pins, sizeof(data_pins));

    panel_config.flags.fb_in_psram = 1;

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (err != ESP_OK) {
        Serial.printf("esp_lcd_new_rgb_panel failed: %s\n", esp_err_to_name(err));
        return false;
    }
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

    // PSRAM survives a soft reset, so the framebuffer still holds the previous image
    // and the panel starts scanning it immediately. Blank it before anything is shown.
    void *fb = NULL;
    if (esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 1, &fb) == ESP_OK && fb) {
        memset(fb, 0, (size_t)SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    }

    const uint32_t h_total = SCREEN_WIDTH + panel_config.timings.hsync_front_porch +
                             panel_config.timings.hsync_pulse_width +
                             panel_config.timings.hsync_back_porch;
    const uint32_t v_total = SCREEN_HEIGHT + panel_config.timings.vsync_front_porch +
                             panel_config.timings.vsync_pulse_width +
                             panel_config.timings.vsync_back_porch;
    Serial.printf("RGB panel ready: %.1f Hz refresh\n",
                  (float)panel_config.timings.pclk_hz / (h_total * v_total));

    // Internal RAM, not PSRAM: a PSRAM draw buffer contends with the panel's own
    // DMA for PSRAM bandwidth and starves it mid-line, shifting the picture sideways.
    buf1 = (lv_color_t *)heap_caps_malloc(SCREEN_WIDTH * BUF_LINES * sizeof(lv_color_t),
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf1) {
        Serial.println("LVGL buffer alloc failed");
        return false;
    }

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    touch.begin(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
    touch.dumpConfig();

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Left off until the first frame is rendered; main turns it on. Avoids showing a
    // partially drawn screen during the initial top-to-bottom render.
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);

    Serial.println("Display initialized");
    return true;
}

void DisplayDriver::flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(disp);
}

// On/off only for now. GPIO 2 drives the enable pin of the backlight's boost driver,
// which does accept PWM dimming at low frequency (the factory demo uses 300 Hz); at
// 20 kHz the driver never finishes starting up and the backlight goes fully off.
void DisplayDriver::setBrightness(uint8_t brightness) {
    digitalWrite(TFT_BL, brightness > 0 ? HIGH : LOW);
}

void DisplayDriver::touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    static uint16_t last_x = 0, last_y = 0;
    uint16_t x, y;

    if (touch.read(&x, &y)) {
        if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT) {
            last_x = x;
            last_y = y;
        }
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}
