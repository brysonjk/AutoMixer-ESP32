#ifndef VALVE_CONTROL_H
#define VALVE_CONTROL_H

#include <Arduino.h>

// Valves: Kelly Pneumatics Miniature Proportional Valve, 0-10 VDC coil version, 1.8 W
// max (~180 mA, ~55 ohm), flow proportional to coil voltage. Each coil is switched
// low-side by an AOD4184 straight from the 12 V supply, so the PWM duty sets the average
// coil voltage.
//
// 1 kHz: the coil's inductance smooths it into a near-steady current, while the small
// residual ripple dithers the armature and cuts the 2-10% hysteresis the datasheet lists.
// Tune against real flow if the valve buzzes or responds unevenly.
#define PWM_FREQ 1000
// 10 bits: 0.1% duty steps. At 1 kHz the LEDC timer supports up to 16.
#define PWM_RESOLUTION 10

// Duty at a valve command of 100%, so a 10 V coil can run from the 12 V supply.
//   0.83 (10/12) averages 10 V, which heats the coil as 10 V DC does IF its inductance
//        smooths the PWM into a steady current.
//   0.69 (1.8 W * 55 ohm / 12^2) keeps the coil within 1.8 W even with no smoothing.
// The coil's inductance is unknown, so this starts at the safe value. Raise it toward
// 0.83 only after a valve held at 100% for 30 min runs no hotter than on 10 V DC.
// Assumes a REGULATED 12 V supply: on a 24 V supply this would put ~17-20 V on the coils.
#define VALVE_MAX_DUTY 0.69f

// Valve pins, both on board headers. GPIO 10 (the original O2 pin) turned out to reach
// no header at all, only the SD-card slot.
//   O2 -> GPIO 17 (P5, beside the valve boards' GND; P4 carries the same net). No
//         pull-up, so it doesn't float high at boot; the valve that matters most gets
//         the pin that behaves best.
//   He -> GPIO 11 (P2). Shares the SD slot's MOSI line and its 10k pull-up to 3.3 V, so
//         it sits HIGH through reset, the bootloader, and all of flashing until the
//         firmware drives it. The 1k pull-down in HARDWARE_SETUP.md covers that window.
#define VALVE_O2_PIN 17
#define VALVE_HE_PIN 11

class ValveController {
public:
    ValveController();

    // Drives both valve pins LOW. Call first thing in setup(), before anything slow,
    // to end the pull-up window as early as firmware can. It cannot cover the window
    // before firmware starts; that needs a pull-down on the driver input (see
    // HARDWARE_SETUP.md).
    static void holdClosed();

    void begin();

    // Set valve opening percentage (0-100%)
    void setO2Valve(float percent);
    void setHeValve(float percent);

    // Close all valves (safety)
    void closeAllValves();

    // Get current valve positions
    float getO2ValvePosition() { return o2_position; }
    float getHeValvePosition() { return he_position; }

private:
    float o2_position;  // 0-100%
    float he_position;  // 0-100%


    uint32_t percentToPWM(float percent);
};

#endif
