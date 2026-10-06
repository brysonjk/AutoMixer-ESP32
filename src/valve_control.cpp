#include "valve_control.h"
#include <driver/gpio.h>

ValveController::ValveController() : o2_position(0.0), he_position(0.0) {
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

    Serial.println("Valve controller initialized");
}

void ValveController::setO2Valve(float percent) {
    // Clamp to valid range
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    const uint32_t pwm_value = percentToPWM(percent);
    const bool changed = pwm_value != percentToPWM(o2_position);
    o2_position = percent;

    ledcWrite(VALVE_O2_PIN, pwm_value);

    if (changed) Serial.printf("O2 valve %.1f%% (PWM %u)\n", percent, (unsigned)pwm_value);
}

void ValveController::setHeValve(float percent) {
    // Clamp to valid range
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    const uint32_t pwm_value = percentToPWM(percent);
    const bool changed = pwm_value != percentToPWM(he_position);
    he_position = percent;

    ledcWrite(VALVE_HE_PIN, pwm_value);

    if (changed) Serial.printf("He valve %.1f%% (PWM %u)\n", percent, (unsigned)pwm_value);
}

void ValveController::closeAllValves() {
    setO2Valve(0);
    setHeValve(0);
}

uint32_t ValveController::percentToPWM(float percent) {
    // HIGH = valve driven. Matches an N-channel AOD4184 switching the valve low-side.
    // 100% maps to VALVE_MAX_DUTY, not full on, to keep the 10 V coils within rating.
    const uint32_t full = (1u << PWM_RESOLUTION) - 1;
    return (uint32_t)lroundf(percent / 100.0f * VALVE_MAX_DUTY * full);
}
