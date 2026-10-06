#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

// GT911 capacitive controller, sharing the I2C bus with the ADS1115.
class TouchGT911 {
public:
    TouchGT911();
    bool begin(int sda, int scl, int rst, int intPin);
    bool isAvailable() { return available; }

    // Returns true and fills x/y while a finger is down.
    bool read(uint16_t* x, uint16_t* y);

    void dumpConfig();

private:
    bool available;
    uint8_t addr;

    uint16_t last_x, last_y;
    bool pressed;

    bool probe(uint8_t address);
    bool readRegs(uint16_t reg, uint8_t* buf, size_t len);
    bool writeReg(uint16_t reg, uint8_t value);
};

#endif
