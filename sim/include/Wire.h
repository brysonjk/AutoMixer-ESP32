// Browser simulator: the I2C bus has nothing on it; the ADS1115 stand-in answers directly.
#pragma once
#include "Arduino.h"

class TwoWire {
public:
    bool begin(int, int) { return true; }
    void setClock(uint32_t) {}
};
extern TwoWire Wire;
