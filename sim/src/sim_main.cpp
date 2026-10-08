// Browser simulator entry point: runs the firmware's own setup() and loop().
#include <emscripten.h>
#include <lvgl.h>
#include "Arduino.h"
#include "Preferences.h"
#include "valve_control.h"

void setup();
void loop();

extern bool sim_compressor_running;
extern int sim_touch_x, sim_touch_y;
extern bool sim_touch_down;
extern const float SIM_O2_CELL_AIR_MV;
extern const float SIM_HE_CELL_AIR_MV;

extern "C" {
EMSCRIPTEN_KEEPALIVE void sim_pointer(int x, int y, int down) {
    sim_touch_x = x;
    sim_touch_y = y;
    sim_touch_down = down != 0;
}
EMSCRIPTEN_KEEPALIVE void sim_set_compressor(int running) { sim_compressor_running = running != 0; }
EMSCRIPTEN_KEEPALIVE int sim_compressor() { return sim_compressor_running ? 1 : 0; }
}

// The firmware's loop() advances LVGL's clock 5 ms per pass, so it runs as many passes
// per animation frame as real time allows, keeping LVGL's clock in step with the wall.
static uint32_t last_ms = 0;
static void frame() {
    const uint32_t now = millis();
    uint32_t passes = (now - last_ms) / 5;
    if (passes == 0) return;
#ifndef SIM_TIME_SCALE
#define SIM_TIME_SCALE 1
#endif
    // After a stalled tab, catch up gently (scaled up when the clock runs fast).
    if (passes > 12 * SIM_TIME_SCALE) passes = 12 * SIM_TIME_SCALE;
    last_ms += passes * 5;
    if (now - last_ms > 60 * SIM_TIME_SCALE) last_ms = now;
    while (passes--) loop();
}

extern const float SIM_VALVE_CRACK;

int main() {
    // Pretend both cells were calibrated in air before, so the demo can blend straight
    // away (Calibrate still works). Same keys the firmware's sensors.cpp reads.
    Preferences prefs;
    prefs.begin("cal", false);
    prefs.putFloat("o2_air", SIM_O2_CELL_AIR_MV);
    prefs.putFloat("he_air", SIM_HE_CELL_AIR_MV);
    prefs.end();
    // And as if both valves' start points had been found on Setup > Valve test, at the
    // simulated valves' dead band (sim_world.cpp).
    prefs.begin("valves", false);
    prefs.putFloat("o2_duty", SIM_VALVE_CRACK * VALVE_MAX_DUTY);
    prefs.putFloat("he_duty", SIM_VALVE_CRACK * VALVE_MAX_DUTY);
    prefs.end();
    // Sample maintenance counts, so the demo's Setup > Maintenance page has something to
    // show. Nothing is overdue, so the main screen still opens on "Ready".
    prefs.begin("maint", false);
    prefs.putUInt("total_s", 412u * 3600u + 1800u);
    prefs.putUInt("filter_s", 18u * 3600u + 1440u);
    prefs.putUShort("filter_lim", 25);
    prefs.putUInt("oil_s", 112u * 3600u);
    prefs.putUShort("oil_lim", 250);
    prefs.end();

    setup();
    last_ms = millis();
    emscripten_set_main_loop(frame, 0, false);
    return 0;
}
