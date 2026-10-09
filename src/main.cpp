#include <Arduino.h>
#include <lvgl.h>
#include "display_driver.h"
#include "sensors.h"
#include "valve_control.h"
#include "wifi_manager.h"
#include "gui.h"
#include "web_dashboard.h"
#include "maintenance.h"
#include <esp_system.h>
#include <Preferences.h>

static OxygenSensor sensors;
static ValveController valves;
static WifiManager wifi;
static Maintenance maintenance;
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

// Target ramp, in percentage points per second. A step from air to 32% would ask the
// P term for ~45% opening at once; with the cell lagging the gas by several seconds the
// mix runs well past target before the loop sees it. Instead each loop chases a target
// that climbs from the reading at this rate, so the gap it acts on stays small. A lower
// target is taken at once.
#ifndef TARGET_RAMP_PER_S
#define TARGET_RAMP_PER_S 1.0f
#endif

// Learned feedforward. Holding a mix needs the valve well open (32% O2 took ~87% on the
// real rig), and an integral building that from zero is slow: near the top of a valve's
// range each extra percent adds little flow, so the last points crawl. Instead each loop
// adds the opening it expects to need for the target it is chasing, and the integral
// only trims the difference. The expectation is learned: once a blend has held its
// target for a while, the opening it needed divided by the gas that target demands is
// saved, and later blends scale it to their own target.
//
// Demand is the share of the gas stream this valve has to supply: for oxygen, the gap
// between the target and the O2 already there, over what pure O2 could add; for helium,
// the target itself.
static const float LEARN_BAND = 0.3f;            // points from target counted as settled
static const uint32_t LEARN_AFTER_MS = 20000;    // settled this long before learning
static const float LEARN_MIN_DEMAND = 0.03f;     // too small a demand to learn from reliably
// Share of the learned opening applied up front. Less than all of it, so the mix comes in
// from below and the integral closes the last of the gap: the P term is still reacting
// to the cell's lag on top of it, and a gain learned at one target overestimates for a
// leaner one on a valve whose flow flattens toward the top. Oxygen must not overshoot.
#ifndef FEEDFORWARD_SHARE
#define FEEDFORWARD_SHARE 0.85f
#endif

// Safeguards against a learned opening that no longer fits, e.g. the unit moved to a
// smaller compressor or a lower regulator pressure.
//   Overshoot guard: a reading this far past the target drops the learned opening for
//     the rest of the blend (the integral finishes from below) and cuts the saved value.
//   O2 ceiling: an O2 reading this far past target shuts the O2 valve until the mix is
//     back at target, whatever the cause.
static const float OVERSHOOT_GUARD = 0.3f;   // the cell lags the gas, so trip early
static const float OVERSHOOT_CUT = 0.5f;
static const float O2_CEILING = 2.0f;

struct PiLoop {
    const char* name;
    bool helium;
    float integral = 0.0f;   // valve % from the I term: trims the feedforward either way
    uint32_t last_ms = 0;
    float ramped = NAN;      // the target this loop is chasing right now
    float last_p = 0.0f;     // last terms and output, for the blend log
    float last_ff = 0.0f;
    float last_out = 0.0f;
    uint32_t settled_since = 0;
    float settled_out = 0.0f;   // output and reading when the current settled stretch began
    float settled_reading = 0.0f;
    bool learned_this_run = false;
    bool ff_off = false;        // learned opening dropped for this blend (overshoot)

    PiLoop(const char* n, bool he) : name(n), helium(he) {}

    void reset() {
        integral = 0.0f;
        last_ms = 0;
        ramped = NAN;
        settled_since = 0;
        learned_this_run = false;
        ff_off = false;
    }

    // Valve shut from outside (the O2 ceiling): start the trim over and leave the learned
    // opening out for the rest of this blend.
    void hold() {
        integral = 0.0f;
        ff_off = true;
        settled_since = 0;
    }

    // Valve command 0-100 for this loop. `base` and `span` turn a target into demand:
    // demand = (target - base) / span. A faulted reading (NAN) resets the loop and closes
    // the valve: with no trustworthy feedback it would be running blind.
    float output(float target, float actual, float base, float span) {
        if (isnan(actual) || isnan(base)) {
            reset();
            return 0.0f;
        }
        const uint32_t now = millis();
        const float dt = last_ms == 0 ? 0.0f : (now - last_ms) / 1000.0f;
        last_ms = now;

        // Start the ramp at the reading, climb toward the target, drop to it at once.
        if (isnan(ramped)) ramped = actual;
        ramped = fminf(target, ramped + TARGET_RAMP_PER_S * dt);

        const float gain = ff_off ? 0.0f : valves.learnedGain(helium);
        if (gain > 0.0f && actual > target + OVERSHOOT_GUARD) {
            // The learned opening is too much for the gas as it is now.
            ff_off = true;
            valves.cutLearned(helium, OVERSHOOT_CUT);
            Serial.printf("%s loop overshot to %.1f%% (target %.0f%%): learned opening cut to %.0f%%%s\n",
                          name, actual, target, OVERSHOOT_CUT * 100.0f,
                          valves.learningLocked() ? " for this blend" : "");
        }
        const float ff = ff_off ? 0.0f
                                : FEEDFORWARD_SHARE * gain * fmaxf(0.0f, (ramped - base) / span);
        const float error = ramped - actual;
        const float p = error * CONTROL_GAIN;
        const float unclamped = ff + p + integral;
        // Anti-windup: only integrate while the valve isn't already pinned in the
        // direction the error pushes, or the I term would pile up behind a saturated
        // valve and overshoot once the gap closes. Overshoot (negative error) still
        // bleeds it down.
        const bool pinned_open = unclamped >= 100.0f && error > 0.0f;
        const bool pinned_shut = unclamped <= 0.0f && error < 0.0f;
        // With a learned feedforward carrying the climb, the gap while the target is still
        // ramping is mostly the cell lagging the gas. Integrating it would wind up an
        // opening the mix doesn't need and run past target once the ramp stops.
        const bool ramping_on_ff = ff > 0.0f && ramped < target;
        if (!pinned_open && !pinned_shut && !ramping_on_ff) integral += error * CONTROL_KI * dt;
        integral = constrain(integral, -100.0f, 100.0f);
        last_p = p;
        last_ff = ff;
        last_out = constrain(ff + p + integral, 0.0f, 100.0f);
        learn(target, actual, base, span, now);
        return last_out;
    }

    void learn(float target, float actual, float base, float span, uint32_t now) {
        // Nothing to learn from a blend the guard or ceiling stepped into: the valve was
        // pulled back and the mix is drifting down, not holding. The next blend learns.
        if (valves.learningLocked() || ff_off) return;
        const bool settled = ramped >= target && fabsf(actual - target) <= LEARN_BAND &&
                             last_out > 0.0f && last_out < 99.0f;
        if (!settled) {
            settled_since = 0;
            return;
        }
        // Settled means the valve has stopped moving too, not just the reading passing
        // through the band on its way somewhere.
        if (settled_since == 0 || fabsf(last_out - settled_out) > 2.0f ||
            fabsf(actual - settled_reading) > 0.2f) {
            settled_since = now | 1;
            settled_out = last_out;
            settled_reading = actual;
        }
        if (learned_this_run || now - settled_since < LEARN_AFTER_MS) return;
        learned_this_run = true;
        const float demand = (target - base) / span;
        if (!(demand >= LEARN_MIN_DEMAND)) return;
        const float old_gain = valves.learnedGain(helium);
        const float gain = last_out / demand;
        if (fabsf(gain - old_gain) <= 0.02f * gain) return;   // close enough: spare the flash
        // Move the integral by what the feedforward gains, so the output doesn't jump.
        if (!ff_off) integral -= FEEDFORWARD_SHARE * (gain - old_gain) * fmaxf(0.0f, (ramped - base) / span);
        valves.saveLearned(helium, gain, target, last_out);
        Serial.printf("%s loop learned: %.0f%% holds %.1f%% (feedforward %.0f per unit demand)\n",
                      name, last_out, target, gain);
    }
};

static PiLoop o2_loop("O2", false), he_loop("He", true);

// A target at or below this O2 percentage, with no helium, is plain air: there is
// nothing to add, so that valve stays shut instead of being nudged to its start point
// by a loop chasing a tenth of a percent. The status panel calls this "Passing air".
static const float AIR_TARGET_O2 = 21.0f;

// O2 ceiling: latched while the final O2 is more than O2_CEILING over target, cleared
// once it is back at target. While latched the O2 valve stays shut.
static bool o2_over = false;

// With the O2 valve shut the reading has to fall. If it hasn't come down by
// CEILING_DROP within CEILING_TRIP_MS, oxygen is getting in some other way (a valve stuck
// open, a leak past it): emergency stop, which needs the operator to resume.
static const uint32_t CEILING_TRIP_MS = 15000;
static const float CEILING_DROP = 0.5f;
static uint32_t ceiling_since = 0;
static float ceiling_o2 = 0.0f;

// Hard limit: O2 this far over the O2 wheel's limit while gas is flowing is an emergency
// stop, whatever the target. Held for LIMIT_TRIP_MS so a single noisy reading can't trip it.
static const float LIMIT_MARGIN = 2.0f;
static const uint32_t LIMIT_TRIP_MS = 2000;
static uint32_t over_limit_since = 0;

static void closeValvesAndReset();

static void emergencyStop(const char* reason) {
    closeValvesAndReset();
    gui->triggerEmergencyStop(reason);
}

static bool o2OverCeiling(float o2, float o2_target) {
    const uint32_t now = millis();
    if (!o2_over && o2 > o2_target + O2_CEILING) {
        o2_over = true;
        o2_loop.hold();
        ceiling_since = now;
        ceiling_o2 = o2;
        Serial.printf("O2 %.1f%% is over %.0f%% + %.0f: O2 valve held shut\n", o2, o2_target, O2_CEILING);
    } else if (o2_over && o2 <= o2_target) {
        o2_over = false;
        Serial.println("O2 back at target: O2 valve released");
    }
    if (o2_over) {
        // Still rising restarts nothing: the window runs from the highest reading seen,
        // so a reading that climbs for 15 s trips just as surely as one that sits still.
        if (o2 > ceiling_o2) ceiling_o2 = o2;
        if (o2 <= ceiling_o2 - CEILING_DROP) {
            ceiling_since = now;
            ceiling_o2 = o2;
        } else if (now - ceiling_since >= CEILING_TRIP_MS) {
            char reason[112];
            snprintf(reason, sizeof(reason),
                     "O2 at %.1f%% didn't fall in 15 s with its valve shut. Check the O2 valve.", o2);
            emergencyStop(reason);
            return true;
        }
    }
    gui->setO2OverTarget(o2_over);
    return o2_over;
}

static bool o2OverLimit(float o2) {
    const float limit = gui->oxygenLimit() + LIMIT_MARGIN;
    if (isnan(o2) || o2 <= limit) {
        over_limit_since = 0;
        return false;
    }
    const uint32_t now = millis();
    if (over_limit_since == 0) over_limit_since = now ? now : 1;
    if (now - over_limit_since < LIMIT_TRIP_MS) return false;
    char reason[112];
    snprintf(reason, sizeof(reason), "O2 reached %.1f%%, over the %u%% limit. Check the O2 valve.", o2,
             gui->oxygenLimit());
    emergencyStop(reason);
    return true;
}

// Every path that forces the valves shut also clears both integrals, so blending
// resumes from zero rather than from an opening built up before the stop.
static void closeValvesAndReset() {
    valves.closeAllValves();
    o2_loop.reset();
    he_loop.reset();
    o2_over = false;
    over_limit_since = 0;
    gui->setO2OverTarget(false);
}

// Once a second while a loop is blending: what it chases, sees and commands, for tuning
// CONTROL_GAIN / CONTROL_KI against the real valves and plumbing.
static void logBlend(const char* name, const PiLoop& loop, float target, float reading,
                     float opening) {
    static uint32_t last_ms[2] = {0, 0};
    const int i = name[0] == 'H' ? 1 : 0;
    const uint32_t now = millis();
    if (now - last_ms[i] < 1000) return;
    last_ms[i] = now;
    Serial.printf("blend %s t=%lu target %.0f ramp %.1f read %.2f FF %.1f P %.1f I %.1f out %.1f open %.1f\n",
                  name, (unsigned long)(now / 1000), target, loop.ramped, reading, loop.last_ff,
                  loop.last_p, loop.integral, loop.last_out, opening);
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
        gui->emergencyStopped() || !compressor_running || gui->fillMode()) {
        closeValvesAndReset();
        return;
    }

    // Setup > Valve test drives one valve by hand, under the same guards as blending.
    if (gui->valveTestActive()) {
        o2_loop.reset();
        he_loop.reset();
        const float opening = gui->valveTestOpening();
        valves.setO2Opening(gui->valveTestHelium() ? 0.0f : opening);
        valves.setHeOpening(gui->valveTestHelium() ? opening : 0.0f);
        return;
    }

    const float o2 = sensors.getOxygenPercent();
    const float o2_target = gui->getOxygenTarget();
    if (o2OverLimit(o2)) return;
    const float he_target = gui->heliumEnabled() ? gui->getHeliumTarget() : 0.0f;
    // Helium dilutes the oxygen, so at a 21% O2 target the O2 valve still has work to do
    // once there is helium in the mix.
    const bool o2_needed = o2_target > AIR_TARGET_O2 || he_target > 0.0f;
    if (!o2_needed) o2_loop.reset();

    // Nitrox only: no helium cell is needed, and the helium valve stays shut.
    if (!gui->heliumEnabled()) {
        valves.setHeValve(0);
        he_loop.reset();
        const bool over = o2OverCeiling(o2, o2_target);
        if (gui->emergencyStopped()) return;
        valves.setO2Valve(o2_needed && !over ? o2_loop.output(o2_target, o2, AIR_O2_PERCENT,
                                                               100.0f - AIR_O2_PERCENT)
                                             : 0.0f);
        if (o2_needed) logBlend("O2", o2_loop, o2_target, o2, valves.getO2ValvePosition());
        return;
    }

    // Either cell faulting shuts both valves: helium with no oxygen control, or oxygen
    // with no idea how much helium it's diluting, is not a mix anyone should breathe.
    const float he_projected = sensors.heliumAfterO2(o2_target);
    if (isnan(o2) || isnan(he_projected)) {
        closeValvesAndReset();
        return;
    }

    // Oxygen goes into gas the helium has already diluted: what the after-He cell reads.
    const float o2_base = sensors.getHeCellO2Percent();
    const bool over = o2OverCeiling(o2, o2_target);
    if (gui->emergencyStopped()) return;
    valves.setO2Valve(o2_needed && !over ? o2_loop.output(o2_target, o2, o2_base, 100.0f - o2_base)
                                         : 0.0f);
    if (o2_needed) logBlend("O2", o2_loop, o2_target, o2, valves.getO2ValvePosition());
    if (he_target <= 0.0f) {
        valves.setHeValve(0);
        he_loop.reset();
        return;
    }
    // Helium goes in first, so it's steered on where it will land once the O2 loop hits
    // its target, not on the current final reading. That keeps the helium loop off the
    // oxygen loop's transients: with the final reading, every O2 overshoot would read
    // as too little helium and open the helium valve.
    valves.setHeValve(he_loop.output(he_target, he_projected, 0.0f, 100.0f));
    logBlend("He", he_loop, he_target, he_projected, valves.getHeValvePosition());
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
    maintenance.begin();
    Serial.printf("feedforward O2 %.0f, He %.0f per unit demand, learning %s\n",
                  valves.learnedGain(false), valves.learnedGain(true),
                  valves.learningLocked() ? "locked" : "automatic");
    wifi.begin();

    gui = new FillStationGUI(&sensors, &valves, &wifi);
    gui->setMaintenance(&maintenance);
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
    maintenance.tick(compressor_running);

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
