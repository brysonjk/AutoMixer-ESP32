// Browser simulator: the RGB panel is a <canvas>, the GT911 is the mouse or finger.
#include <emscripten.h>
#include "display_driver.h"

// Copies one flushed area, RGB565, into the page's canvas.
// The page defines globalThis.simBlit(heapU16, pixelOffset, x, y, w, h).
EM_JS(void, sim_blit, (const uint16_t* px, int x, int y, int w, int h), {
    if (typeof globalThis.simBlit === 'function') globalThis.simBlit(HEAPU16, px >> 1, x, y, w, h);
});

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t* buf1;

int sim_touch_x = 0, sim_touch_y = 0;
bool sim_touch_down = false;

bool DisplayDriver::init() {
    lv_init();
    const uint32_t px = SCREEN_WIDTH * SCREEN_HEIGHT;
    buf1 = (lv_color_t*)malloc(px * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, px);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);

    Serial.println("Display initialized (browser canvas)");
    return true;
}

void DisplayDriver::flush_cb(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    sim_blit((const uint16_t*)color_p, area->x1, area->y1, area->x2 - area->x1 + 1,
             area->y2 - area->y1 + 1);
    lv_disp_flush_ready(disp);
}

void DisplayDriver::setBrightness(uint8_t) {}

void DisplayDriver::touchpad_read(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->point.x = sim_touch_x;
    data->point.y = sim_touch_y;
    data->state = sim_touch_down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
