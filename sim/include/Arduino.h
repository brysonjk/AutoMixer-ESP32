// Browser simulator: the slice of the Arduino core the firmware uses, backed by the
// browser instead of ESP32 hardware. Only what main.cpp, gui.cpp, sensors.cpp and
// valve_control.cpp actually call is here.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The ESP32 core's Arduino.h brings these in too; main.cpp relies on it.
#include "esp_heap_caps.h"

using std::isnan;
using std::max;
using std::min;

#define LOW 0
#define HIGH 1
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

class String {
public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(char c) : s_(1, c) {}
    String(int v) : s_(std::to_string(v)) {}
    String(unsigned v) : s_(std::to_string(v)) {}
    String(long v) : s_(std::to_string(v)) {}
    String(unsigned long v) : s_(std::to_string(v)) {}
    String(float v, int decimals = 2) { fmt(v, decimals); }
    String(double v, int decimals = 2) { fmt(v, decimals); }

    const char* c_str() const { return s_.c_str(); }
    unsigned length() const { return (unsigned)s_.size(); }
    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
    void replace(char from, char to) { std::replace(s_.begin(), s_.end(), from, to); }

    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* o) { s_ += o; return *this; }
    String& operator+=(char c) { s_ += c; return *this; }
    friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
    friend String operator+(const String& a, const char* b) { return String(a.s_ + b); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s_); }
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator==(const char* o) const { return s_ == o; }
    bool operator!=(const char* o) const { return s_ != o; }

private:
    std::string s_;
    void fmt(double v, int decimals) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        s_ = buf;
    }
};

class HardwareSerial {
public:
    void begin(unsigned long) {}
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void print(const char* s) { std::fputs(s, stdout); }
    void print(const String& s) { print(s.c_str()); }
    void println() { std::puts(""); }
    void println(const char* s) { std::puts(s); }
    void println(const String& s) { std::puts(s.c_str()); }
};
extern HardwareSerial Serial;

uint32_t millis();
void delay(uint32_t ms);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);
void enableLoopWDT();
// strlcpy comes from Emscripten's libc (musl declares it for C++ builds).
