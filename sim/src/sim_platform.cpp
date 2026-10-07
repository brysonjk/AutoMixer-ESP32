// Browser simulator: timing, pins and PWM for the Arduino calls the firmware makes.
#include <emscripten.h>
#include "Arduino.h"
#include "Wire.h"
#include "esp_random.h"

HardwareSerial Serial;
TwoWire Wire;

int HardwareSerial::printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vprintf(fmt, ap);
    va_end(ap);
    std::fflush(stdout);
    return n;
}

// Clock speed-up for headless tests: the firmware sees SIM_TIME_SCALE seconds pass per
// real second, so a long blend test fits in a short run. 1 (real time) for the demo.
#ifndef SIM_TIME_SCALE
#define SIM_TIME_SCALE 1
#endif
static const double t0 = emscripten_get_now();
uint32_t millis() { return (uint32_t)((emscripten_get_now() - t0) * SIM_TIME_SCALE); }

// A browser can't block; the firmware's delays only pace its own loop, which the
// simulator paces from requestAnimationFrame instead.
void delay(uint32_t) {}

// Compressor current switch on GPIO 12: LOW while the compressor runs.
bool sim_compressor_running = false;
static const uint8_t COMPRESSOR_PIN = 12;

void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin) {
    if (pin == COMPRESSOR_PIN) return sim_compressor_running ? LOW : HIGH;
    return HIGH;
}

// PWM duty per pin, read by the gas model to decide how far each valve is open.
uint32_t sim_pwm_duty[64];
bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
bool ledcWrite(uint8_t pin, uint32_t duty) {
    if (pin < 64) sim_pwm_duty[pin] = duty;
    return true;
}

void enableLoopWDT() {}

uint32_t esp_random() { return (uint32_t)(emscripten_random() * 4294967295.0); }
