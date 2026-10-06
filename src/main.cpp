#include <Arduino.h>
#include <lvgl.h>
#include "display_driver.h"
#include "sensors.h"
#include "valve_control.h"
#include "wifi_manager.h"
#include "gui.h"
#include "web_dashboard.h"
#include <esp_system.h>

static OxygenSensor sensors;
static ValveController valves;
static WifiManager wifi;
static FillStationGUI* gui = nullptr;
static WebDashboard* web = nullptr;

static const uint32_t GUI_INTERVAL_MS = 100;
// Four conversions take ~36 ms; at 250 ms the smoothing filter gets several samples per
// time constant (see O2_SMOOTHING_S) without holding up the touchscreen much.
static const uint32_t SENSOR_INTERVAL_MS = 250;

// Compressor-running input: a current switch's contact between this pin and GND. GPIO 12
// is on header P2 and already has a 10k pull-up on the board (it's the SD slot's clock
// line), so it reads LOW while the compressor runs and HIGH if the switch is unplugged.
static const uint8_t COMPRESSOR_PIN = 12;
// The contact must hold a new state this long before it counts, so relay bounce and
// noise on a long cable can't flicker the valves.
static const uint32_t COMPRESSOR_DEBOUNCE_MS = 200;
static bool compressor_running = false;

// PI control, one loop per gas. Tune both against real hardware before trusting a fill.
//
// Proportional alone settles short of the target: holding a mix needs the valve partly
// open, and a P-only loop only opens it while there is a gap, so it parks where
// gap x gain equals the opening it needs (33% came to rest near 31% in the simulator).
// The integral term accumulates the remaining gap and supplies that holding opening,
// so the gap closes to zero.
//
// CONTROL_GAIN: valve % per percentage-point of error, acting immediately.
// CONTROL_KI:   valve % per point, per second of that error persisting. Kp/Ki is the
//               integral time: 4 / 0.4 = 10 s to add as much again as the P term.
static const float CONTROL_GAIN = 4.0f;
static const float CONTROL_KI = 0.4f;

struct PiLoop {
    float integral = 0.0f;   // valve % contributed by the I term, kept within 0..100
    uint32_t last_ms = 0;

    void reset() {
        integral = 0.0f;
        last_ms = 0;
    }

    // Valve opening 0-100 for this error. A faulted reading (NAN) resets the loop and
    // closes the valve: with no trustworthy feedback it would be running blind.
    float output(float target, float actual) {
        if (isnan(actual)) {
            reset();
            return 0.0f;
        }
        const uint32_t now = millis();
        const float dt = last_ms == 0 ? 0.0f : (now - last_ms) / 1000.0f;
        last_ms = now;

        const float error = target - actual;
        const float p = error * CONTROL_GAIN;
        const float unclamped = p + integral;
        // Anti-windup: only integrate while the valve isn't already pinned in the
        // direction the error pushes, or the I term would pile up behind a saturated
        // valve and overshoot once the gap closes. Overshoot (negative error) still
        // bleeds it down.
        const bool pinned_open = unclamped >= 100.0f && error > 0.0f;
        const bool pinned_shut = unclamped <= 0.0f && error < 0.0f;
        if (!pinned_open && !pinned_shut) integral += error * CONTROL_KI * dt;
        integral = constrain(integral, 0.0f, 100.0f);
        return constrain(p + integral, 0.0f, 100.0f);
    }
};

static PiLoop o2_loop, he_loop;

// Every path that forces the valves shut also clears both integrals, so blending
// resumes from zero rather than from an opening built up before the stop.
static void closeValvesAndReset() {
    valves.closeAllValves();
    o2_loop.reset();
    he_loop.reset();
}

static void updateCompressor() {
    static bool last_raw = false;
    static uint32_t changed_at = 0;
    const bool raw = digitalRead(COMPRESSOR_PIN) == LOW;
    const uint32_t now = millis();
    if (raw != last_raw) {
        last_raw = raw;
        changed_at = now;
    }
    if (raw != compressor_running && now - changed_at >= COMPRESSOR_DEBOUNCE_MS) {
        compressor_running = raw;
        gui->setCompressorRunning(compressor_running);
        Serial.printf("compressor %s\n", compressor_running ? "running" : "stopped");
    }
}

// Fails closed: with no sensor feedback there is no way to know the mix, so the valves
// stay shut rather than running open-loop on a gas system.
static void updateBlendControl() {
    // Also shut during an emergency stop; while calibrating, since the cells must see
    // plain air rather than injected gas; until the cells have been calibrated once,
    // since on the placeholder calibration the O2 reading can be off by a third; and
    // whenever the compressor isn't running, since with no air drawn through there is
    // nothing to blend into and injected gas would pool at the intake.
    if (!sensors.isAvailable() || sensors.calibrating() || !sensors.isCalibrated() ||
        gui->emergencyStopped() || !compressor_running) {
        closeValvesAndReset();
        return;
    }

    const float o2 = sensors.getOxygenPercent();
    const float o2_target = gui->getOxygenTarget();

    // Nitrox only: no helium cell is needed, and the helium valve stays shut.
    if (!gui->heliumEnabled()) {
        valves.setHeValve(0);
        he_loop.reset();
        valves.setO2Valve(o2_loop.output(o2_target, o2));
        return;
    }

    // Either cell faulting shuts both valves: helium with no oxygen control, or oxygen
    // with no idea how much helium it's diluting, is not a mix anyone should breathe.
    const float he_projected = sensors.heliumAfterO2(o2_target);
    if (isnan(o2) || isnan(he_projected)) {
        closeValvesAndReset();
        return;
    }

    valves.setO2Valve(o2_loop.output(o2_target, o2));
    // Helium goes in first, so it's steered on where it will land once the O2 loop hits
    // its target, not on the current final reading. That keeps the helium loop off the
    // oxygen loop's transients: with the final reading, every O2 overshoot would read
    // as too little helium and open the helium valve.
    valves.setHeValve(he_loop.output(gui->getHeliumTarget(), he_projected));
}

void setup() {
    // Before anything else: the valve pins are pulled high on the board until driven.
    ValveController::holdClosed();

    pinMode(COMPRESSOR_PIN, INPUT_PULLUP);

    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Fill Station ESP32 ===");

    if (!DisplayDriver::init()) {
        Serial.println("Display init failed - halting");
        return;
    }

    sensors.begin();
    valves.begin();
    wifi.begin();

    gui = new FillStationGUI(&sensors, &valves, &wifi);
    gui->init();

    web = new WebDashboard(&sensors, &valves, gui);
    web->begin();

    // A crash, watchdog or brownout reset mid-fill comes back up stopped rather than
    // blending again on its own; the operator resumes at the unit, as after any E-STOP.
    const esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
        reason == ESP_RST_WDT || reason == ESP_RST_BROWNOUT) {
        gui->triggerEmergencyStop();
        Serial.printf("unexpected reset (reason %d): starting in EMERGENCY STOP\n", (int)reason);
    }

    lv_refr_now(NULL);
    DisplayDriver::setBrightness(255);

    // Resets the unit if loop() ever stops returning for 5 s. The valve pins are driven
    // LOW first thing on boot, so a hang ends with the valves closed and, per the check
    // above, the unit latched in E-STOP.
    enableLoopWDT();

    // Internal RAM is the tight budget (display bounce buffers, WiFi, the web task); the
    // largest free block matters as much as the total, since WiFi needs contiguous buffers.
    Serial.printf("Ready. Internal RAM free %u, largest block %u; PSRAM free %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void loop() {
    if (gui == nullptr) {
        delay(1000);
        return;
    }

    const uint32_t now = millis();

    wifi.loop();
    if (web) web->loop();
    sensors.calibrationTick();
    updateCompressor();

    static uint32_t last_sensor = 0;
    if (now - last_sensor >= SENSOR_INTERVAL_MS) {
        last_sensor = now;
        sensors.update();
        updateBlendControl();
    }

    static uint32_t last_gui = 0;
    if (now - last_gui >= GUI_INTERVAL_MS) {
        last_gui = now;
        gui->update();
    }

    lv_timer_handler();
    delay(5);
    lv_tick_inc(5);
}
