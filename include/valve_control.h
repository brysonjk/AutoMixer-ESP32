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
//   0.80 averages 9.6 V: 1.68 W if smoothed, a little headroom under the 1.8 W rating.
//   0.69 (1.8 W * 55 ohm / 12^2) keeps the coil within 1.8 W even with no smoothing.
// Raised from 0.69 to 0.80 after a real blend needed ~94% of the 0.69 range to hold 32%
// at 75 psi. Confirm with a valve held at 100% for 30 min: it must run no hotter than
// on 10 V DC, or go back to 0.69.
// Assumes a REGULATED 12 V supply: on a 24 V supply this would put ~19 V on the coils.
#define VALVE_MAX_DUTY 0.80f

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

// Start points. A proportional valve passes nothing until the coil is strong enough to
// lift the plunger off its seat, often a third of the way up or more. Driven straight
// from the control loop, a valve sits shut through that dead band while the integral
// slowly climbs to it, then flow starts all at once. Each valve's start point is the
// opening (0-100, the same scale as the Valve test page) where its gas first flows;
// any loop command above zero starts there, and 100 still means fully open. Measured on
// Setup > Valve test and stored in NVS namespace "valves" as absolute PWM duty
// ("o2_duty", "he_duty"), so a change to VALVE_MAX_DUTY doesn't move the coil voltage
// a saved start point stands for.
#define VALVE_START_MAX 90.0f
// Loop commands below this are treated as closed, so a loop sitting a hair above zero
// doesn't hold the valve at its start point.
#define VALVE_MIN_COMMAND 0.5f

class ValveController {
public:
    ValveController();

    // Drives both valve pins LOW. Call first thing in setup(), before anything slow,
    // to end the pull-up window as early as firmware can. It cannot cover the window
    // before firmware starts; that needs a pull-down on the driver input (see
    // HARDWARE_SETUP.md).
    static void holdClosed();

    void begin();

    // Control-loop command, 0-100. Above VALVE_MIN_COMMAND it is spread over the range
    // from that valve's start point to fully open; below it the valve is shut.
    void setO2Valve(float command);
    void setHeValve(float command);

    // Raw opening, 0-100, bypassing the start point. For the Valve test page only.
    void setO2Opening(float opening);
    void setHeOpening(float opening);

    // Close all valves (safety)
    void closeAllValves();

    // Opening actually driven, 0-100.
    float getO2ValvePosition() { return o2_position; }
    float getHeValvePosition() { return he_position; }

    float o2StartPoint() const { return o2_start; }
    float heStartPoint() const { return he_start; }
    // Saves a valve's start point (clamped to 0..VALVE_START_MAX); 0 clears it.
    void setStartPoint(bool helium, float opening);

    // Learned holding openings, for main.cpp's control loops: valve command per unit of
    // gas demand, and for display the target and command it was learned at. 0 = none.
    float learnedGain(bool helium) const { return helium ? he_ff : o2_ff; }
    bool learnedAt(bool helium, float* target, float* command) const;
    void saveLearned(bool helium, float gain, float target, float command);
    // Scales a learned gain down after an overshoot; saved unless learning is locked.
    void cutLearned(bool helium, float factor);
    void forgetLearned();
    // Locked keeps the learned values and stops updating them.
    bool learningLocked() const { return learning_locked; }
    void setLearningLocked(bool locked);

private:
    float o2_position;  // 0-100%
    float he_position;  // 0-100%
    float o2_start;     // start points, 0-100%
    float he_start;
    float o2_ff = 0.0f, he_ff = 0.0f;          // learned gains
    float o2_ff_target = 0.0f, he_ff_target = 0.0f;
    float o2_ff_command = 0.0f, he_ff_command = 0.0f;
    bool learning_locked = false;

    void loadStartPoints();
    static float commandToOpening(float command, float start);
    void drive(uint8_t pin, const char* name, float opening, float* position);
    uint32_t percentToPWM(float percent);
};

#endif
