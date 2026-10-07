#include "valve_control.h"
#include <driver/gpio.h>
#include <Preferences.h>

ValveController::ValveController()
    : o2_position(0.0), he_position(0.0), o2_start(0.0), he_start(0.0) {
}

// ESP-IDF calls rather than digitalWrite()/pinMode(): this Arduino core rejects
// digitalWrite() on a pin not yet set up as a GPIO, so the "LOW before OUTPUT" order
// silently didn't happen. Here the output latch is cleared first, then the pin becomes
// an output, so it can never drive HIGH even for an instant.
void ValveController::holdClosed() {
    const gpio_num_t pins[] = {(gpio_num_t)VALVE_O2_PIN, (gpio_num_t)VALVE_HE_PIN};
    for (gpio_num_t pin : pins) gpio_set_level(pin, 0);
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << VALVE_O2_PIN) | (1ULL << VALVE_HE_PIN);
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    for (gpio_num_t pin : pins) gpio_set_level(pin, 0);
}

void ValveController::begin() {
    ledcAttach(VALVE_O2_PIN, PWM_FREQ, PWM_RESOLUTION);
    ledcAttach(VALVE_HE_PIN, PWM_FREQ, PWM_RESOLUTION);

    // Start with valves closed
    closeAllValves();
    loadStartPoints();

    Serial.printf("Valve controller initialized, start points O2 %.0f%%, He %.0f%%\n",
                  o2_start, he_start);
}

// Start points were first saved as an opening under a 0.69 duty cap ("o2_start");
// they are now kept as absolute duty ("o2_duty") and converted to the current cap's
// opening scale on load.
static const float LEGACY_START_DUTY_CAP = 0.69f;

static float loadStartPoint(Preferences& prefs, const char* duty_key, const char* legacy_key) {
    float duty = 0.0f;
    // isKey() first: getFloat() on a missing key logs an NVS error on every boot.
    if (prefs.isKey(duty_key)) {
        duty = prefs.getFloat(duty_key, 0.0f);
    } else if (prefs.isKey(legacy_key)) {
        duty = prefs.getFloat(legacy_key, 0.0f) / 100.0f * LEGACY_START_DUTY_CAP;
        prefs.putFloat(duty_key, duty);
        prefs.remove(legacy_key);
        Serial.printf("valves: start point %s converted to duty %.3f\n", legacy_key, duty);
    }
    return constrain(duty / VALVE_MAX_DUTY * 100.0f, 0.0f, VALVE_START_MAX);
}

static float loadFloat(Preferences& prefs, const char* key) {
    return prefs.isKey(key) ? prefs.getFloat(key, 0.0f) : 0.0f;
}

void ValveController::loadStartPoints() {
    Preferences prefs;
    prefs.begin("valves", false);
    o2_start = loadStartPoint(prefs, "o2_duty", "o2_start");
    he_start = loadStartPoint(prefs, "he_duty", "he_start");
    o2_ff = constrain(loadFloat(prefs, "o2_ff"), 0.0f, 3000.0f);
    he_ff = constrain(loadFloat(prefs, "he_ff"), 0.0f, 3000.0f);
    o2_ff_target = loadFloat(prefs, "o2_ff_t");
    he_ff_target = loadFloat(prefs, "he_ff_t");
    o2_ff_command = loadFloat(prefs, "o2_ff_c");
    he_ff_command = loadFloat(prefs, "he_ff_c");
    learning_locked = prefs.isKey("ff_lock") && prefs.getBool("ff_lock", false);
    prefs.end();
}

bool ValveController::learnedAt(bool helium, float* target, float* command) const {
    *target = helium ? he_ff_target : o2_ff_target;
    *command = helium ? he_ff_command : o2_ff_command;
    return learnedGain(helium) > 0.0f;
}

void ValveController::saveLearned(bool helium, float gain, float target, float command) {
    (helium ? he_ff : o2_ff) = gain;
    (helium ? he_ff_target : o2_ff_target) = target;
    (helium ? he_ff_command : o2_ff_command) = command;
    Preferences prefs;
    prefs.begin("valves", false);
    prefs.putFloat(helium ? "he_ff" : "o2_ff", gain);
    prefs.putFloat(helium ? "he_ff_t" : "o2_ff_t", target);
    prefs.putFloat(helium ? "he_ff_c" : "o2_ff_c", command);
    prefs.end();
}

void ValveController::cutLearned(bool helium, float factor) {
    float& gain = helium ? he_ff : o2_ff;
    gain *= factor;
    (helium ? he_ff_command : o2_ff_command) *= factor;
    if (learning_locked) return;   // this run only: a locked value stays as saved
    Preferences prefs;
    prefs.begin("valves", false);
    prefs.putFloat(helium ? "he_ff" : "o2_ff", gain);
    prefs.putFloat(helium ? "he_ff_c" : "o2_ff_c", helium ? he_ff_command : o2_ff_command);
    prefs.end();
}

void ValveController::forgetLearned() {
    o2_ff = he_ff = o2_ff_target = he_ff_target = o2_ff_command = he_ff_command = 0.0f;
    Preferences prefs;
    prefs.begin("valves", false);
    const char* keys[] = {"o2_ff", "he_ff", "o2_ff_t", "he_ff_t", "o2_ff_c", "he_ff_c"};
    for (const char* k : keys) prefs.remove(k);
    prefs.end();
    Serial.println("valves: learned openings forgotten");
}

void ValveController::setLearningLocked(bool locked) {
    learning_locked = locked;
    Preferences prefs;
    prefs.begin("valves", false);
    prefs.putBool("ff_lock", locked);
    prefs.end();
    Serial.printf("valves: learning %s\n", locked ? "locked" : "automatic");
}

void ValveController::setStartPoint(bool helium, float opening) {
    const float v = constrain(opening, 0.0f, VALVE_START_MAX);
    (helium ? he_start : o2_start) = v;
    Preferences prefs;
    prefs.begin("valves", false);
    prefs.putFloat(helium ? "he_duty" : "o2_duty", v / 100.0f * VALVE_MAX_DUTY);
    prefs.end();
    Serial.printf("%s valve start point %.0f%%\n", helium ? "He" : "O2", v);
}

float ValveController::commandToOpening(float command, float start) {
    if (!(command >= VALVE_MIN_COMMAND)) return 0.0f;   // also catches NAN
    if (command > 100.0f) command = 100.0f;
    return start + (100.0f - start) * command / 100.0f;
}

void ValveController::setO2Valve(float command) { setO2Opening(commandToOpening(command, o2_start)); }
void ValveController::setHeValve(float command) { setHeOpening(commandToOpening(command, he_start)); }
void ValveController::setO2Opening(float opening) { drive(VALVE_O2_PIN, "O2", opening, &o2_position); }
void ValveController::setHeOpening(float opening) { drive(VALVE_HE_PIN, "He", opening, &he_position); }

void ValveController::drive(uint8_t pin, const char* name, float opening, float* position) {
    if (!(opening > 0.0f)) opening = 0.0f;   // also catches NAN
    if (opening > 100.0f) opening = 100.0f;

    const uint32_t pwm_value = percentToPWM(opening);
    const bool changed = pwm_value != percentToPWM(*position);
    *position = opening;

    ledcWrite(pin, pwm_value);

    if (changed) Serial.printf("%s valve %.1f%% (PWM %u)\n", name, opening, (unsigned)pwm_value);
}

void ValveController::closeAllValves() {
    setO2Opening(0);
    setHeOpening(0);
}

uint32_t ValveController::percentToPWM(float percent) {
    // HIGH = valve driven. Matches an N-channel AOD4184 switching the valve low-side.
    // 100% maps to VALVE_MAX_DUTY, not full on, to keep the 10 V coils within rating.
    const uint32_t full = (1u << PWM_RESOLUTION) - 1;
    return (uint32_t)lroundf(percent / 100.0f * VALVE_MAX_DUTY * full);
}
