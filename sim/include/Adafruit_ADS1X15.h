// Browser simulator: an ADS1115 whose inputs come from a simulated gas stream and
// pressure transducers (sim_world.cpp) instead of real wires. Same API subset as the
// Adafruit library, so the firmware's sensors.cpp runs unmodified.
#pragma once
#include "Arduino.h"

typedef enum {
    GAIN_TWOTHIRDS = 0x0000,
    GAIN_ONE = 0x0200,
    GAIN_TWO = 0x0400,
    GAIN_FOUR = 0x0600,
    GAIN_EIGHT = 0x0800,
    GAIN_SIXTEEN = 0x0A00,
} adsGain_t;

#define ADS1X15_REG_CONFIG_MUX_SINGLE_0 (0x4000)
#define ADS1X15_REG_CONFIG_MUX_SINGLE_1 (0x5000)
#define ADS1X15_REG_CONFIG_MUX_SINGLE_2 (0x6000)
#define ADS1X15_REG_CONFIG_MUX_SINGLE_3 (0x7000)
constexpr uint16_t MUX_BY_CHANNEL[] = {
    ADS1X15_REG_CONFIG_MUX_SINGLE_0, ADS1X15_REG_CONFIG_MUX_SINGLE_1,
    ADS1X15_REG_CONFIG_MUX_SINGLE_2, ADS1X15_REG_CONFIG_MUX_SINGLE_3,
};

class Adafruit_ADS1115 {
public:
    bool begin(uint8_t = 0x48) { return true; }
    void setGain(adsGain_t g) { gain_ = g; }
    void startADCReading(uint16_t mux, bool continuous);
    bool conversionComplete() { return true; }
    int16_t getLastConversionResults() { return last_; }
    float computeVolts(int16_t counts);

private:
    adsGain_t gain_ = GAIN_TWOTHIRDS;
    int16_t last_ = 0;
    float fullScale() const;
};
