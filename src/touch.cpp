#include "touch.h"
#include <Wire.h>

static const uint16_t REG_PRODUCT_ID = 0x8140;
static const uint16_t REG_STATUS = 0x814E;

TouchGT911::TouchGT911()
    : available(false), addr(0x5D), last_x(0), last_y(0), pressed(false) {
}

bool TouchGT911::begin(int sda, int scl, int rst, int intPin) {
    Wire.begin(sda, scl);
    Wire.setClock(400000);

    // The GT911 latches its I2C address and enters normal scanning based on the INT
    // level when RST is released: INT low selects 0x5D. Leaving INT floating gets a
    // chip that answers its ID register but never scans.
    if (rst >= 0) {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, LOW);
        if (intPin >= 0) {
            pinMode(intPin, OUTPUT);
            digitalWrite(intPin, LOW);
        }
        delay(20);
        digitalWrite(rst, HIGH);
        delay(10);
        if (intPin >= 0) pinMode(intPin, INPUT);
        delay(100);
    }

    // Which of the two addresses the chip answers on depends on the INT/RST strapping
    // at power-up, which this board doesn't expose, so try both.
    const uint8_t candidates[2] = {0x5D, 0x14};
    for (uint8_t i = 0; i < 2; i++) {
        if (probe(candidates[i])) {
            addr = candidates[i];
            available = true;
            writeReg(0x8040, 0x00);  // command register: normal scanning
            Serial.printf("GT911 touch found at 0x%02X\n", addr);
            return true;
        }
    }

    Serial.println("GT911 touch not found - display will be output only");
    available = false;
    return false;
}

bool TouchGT911::probe(uint8_t address) {
    addr = address;
    uint8_t id[4] = {0};
    if (!readRegs(REG_PRODUCT_ID, id, 4)) return false;
    return id[0] == '9' && id[1] == '1' && id[2] == '1';
}

bool TouchGT911::readRegs(uint16_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool TouchGT911::writeReg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

void TouchGT911::dumpConfig() {
    if (!available) return;

    uint8_t cfg[8] = {0};
    if (!readRegs(0x8047, cfg, 8)) {
        Serial.println("GT911: config read FAILED");
        return;
    }
    Serial.printf("GT911 config: ver=0x%02X xmax=%u ymax=%u touches=%u sw1=0x%02X sw2=0x%02X\n",
                  cfg[0], (uint16_t)cfg[1] | ((uint16_t)cfg[2] << 8),
                  (uint16_t)cfg[3] | ((uint16_t)cfg[4] << 8), cfg[5] & 0x0F, cfg[6], cfg[7]);
}

bool TouchGT911::read(uint16_t* x, uint16_t* y) {
    if (!available) return false;

    // Status and the first contact in one burst: 0x814E status, 0x814F track id,
    // 0x8150-51 x, 0x8152-53 y, 0x8154-55 size.
    uint8_t b[10] = {0};
    if (readRegs(REG_STATUS, b, 10) && (b[0] & 0x80)) {
        const uint8_t points = b[0] & 0x0F;

        if (points > 0) {
            last_x = (uint16_t)b[2] | ((uint16_t)b[3] << 8);
            last_y = (uint16_t)b[4] | ((uint16_t)b[5] << 8);
            pressed = true;
        } else {
            pressed = false;
        }

        writeReg(REG_STATUS, 0);
    }

    // Hold the last known state between updates. The controller only raises the ready
    // flag when something changes, so a stationary finger would otherwise read as
    // released and LVGL would see press/release chatter instead of a drag.
    *x = last_x;
    *y = last_y;
    return pressed;
}
