// Browser simulator: Arduino's IPAddress, enough for the Network page's address fields.
#pragma once
#include "Arduino.h"

class IPAddress {
public:
    IPAddress() : b_{0, 0, 0, 0} {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : b_{a, b, c, d} {}
    // Same byte order as the ESP32 core: the first octet is the low byte.
    IPAddress(uint32_t v) : b_{(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)} {}
    operator uint32_t() const {
        return (uint32_t)b_[0] | ((uint32_t)b_[1] << 8) | ((uint32_t)b_[2] << 16) | ((uint32_t)b_[3] << 24);
    }
    uint8_t operator[](int i) const { return b_[i]; }
    bool operator==(const IPAddress& o) const { return (uint32_t)*this == (uint32_t)o; }

    bool fromString(const char* s) {
        unsigned v[4];
        char tail;
        if (std::sscanf(s, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4) return false;
        for (int i = 0; i < 4; i++) {
            if (v[i] > 255) return false;
            b_[i] = (uint8_t)v[i];
        }
        return true;
    }
    String toString() const {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b_[0], b_[1], b_[2], b_[3]);
        return String(buf);
    }

private:
    uint8_t b_[4];
};
