// Browser simulator: the gas stream and pressure transducers behind the ADS1115.
//
// The firmware's own control loop drives the valves; this turns the PWM it writes into
// what the oxygen cells and transducers would see. It is a rough first-order model, not
// the real plumbing: helium goes in first (seen by the A0 cell), oxygen after it (A1).
#include <cmath>
#include "Adafruit_ADS1X15.h"
#include "sensors.h"
#include "valve_control.h"

extern uint32_t sim_pwm_duty[64];
extern bool sim_compressor_running;

// Air readings of the two simulated cells. sim_main.cpp seeds these as the saved
// calibration so the demo starts ready to blend.
extern const float SIM_O2_CELL_AIR_MV = 10.0f;
extern const float SIM_HE_CELL_AIR_MV = 9.8f;

// Opening at which a simulated valve starts to pass gas. Real proportional valves have a
// dead band like this; the firmware's start points (Setup > Valve test) step over it.
extern const float SIM_VALVE_CRACK = 0.35f;

namespace {
// Plant: O2 share of the final gas at a fully open valve, and how fast the mix at the
// cells follows a valve change. Overridable to try the loop against a slower rig.
#ifndef SIM_O2_FLOW
#define SIM_O2_FLOW 3.16f
#endif
#ifndef SIM_FLOW_SHAPE
#define SIM_FLOW_SHAPE 1.0f   // 1 = linear above the crack point; higher flattens the top
#endif
#ifndef SIM_MIX_TAU_S
#define SIM_MIX_TAU_S 5.0f
#endif
float o2_flow = SIM_O2_FLOW;   // adjustable at run time, for tests
float o2_leak = 0.0f;          // oxygen that gets in with the valve shut: a stuck valve, for tests
float he_share = 0.0f;       // helium fraction of the gas at the A0 cell
float o2_added = 0.0f;       // fraction of the final gas that is added pure oxygen
float bank_psi = 2840.0f;
float fill_psi = 1180.0f;   // a cylinder on the fill whip, filling while the compressor runs
uint32_t last_ms = 0;


// Flow fraction 0..1 from the valve's PWM: nothing below the crack point, then linear.
float valveCommand(uint8_t pin) {
    const float full = (float)((1u << PWM_RESOLUTION) - 1) * VALVE_MAX_DUTY;
    const float opening = std::fmin(1.0f, sim_pwm_duty[pin] / full);
    const float g = std::fmax(0.0f, (opening - SIM_VALVE_CRACK) / (1.0f - SIM_VALVE_CRACK));
    // Real valves flatten near the top: each extra percent of opening adds less flow.
    return 1.0f - std::pow(1.0f - g, SIM_FLOW_SHAPE);
}

float noise(float amplitude) { return amplitude * (float)((std::rand() / (double)RAND_MAX) * 2.0 - 1.0); }

void step() {
    const uint32_t now = millis();
    const float dt = last_ms == 0 ? 0.0f : (now - last_ms) / 1000.0f;
    last_ms = now;
    if (dt <= 0.0f) return;

    // With no compressor drawing air through, fresh air slowly displaces the line.
    const bool flowing = sim_compressor_running;
    const float he_target = flowing ? std::fmin(0.95f, 2.5f * valveCommand(VALVE_HE_PIN)) : 0.0f;
    const float o2_target = flowing ? std::fmin(1.0f, o2_flow * valveCommand(VALVE_O2_PIN) + o2_leak) : 0.0f;
    const float k = 1.0f - std::exp(-dt / SIM_MIX_TAU_S);   // time for the mix to follow a valve change
    he_share += (he_target - he_share) * k;
    o2_added += (o2_target - o2_added) * k;
    if (flowing) bank_psi = std::fmin(4500.0f, bank_psi + 10.0f * dt);
    if (flowing) fill_psi = std::fmin(3400.0f, fill_psi + 6.0f * dt);
}

float o2AtA0() { return AIR_O2_PERCENT * (1.0f - he_share); }
float o2Final() { return o2AtA0() * (1.0f - o2_added) + 100.0f * o2_added; }

// Transducer output (0.5-4.5 V over 0-5000 PSI) as the ADC sees it, through the divider.
float pressureAtAdc(float psi) {
    return (PRESSURE_V_ZERO + psi / PRESSURE_PSI_FULL * (PRESSURE_V_FULL - PRESSURE_V_ZERO)) *
           PRESSURE_DIVIDER;
}

float channelVolts(int channel) {
    step();
    switch (channel) {
        case ADC_CH_OXYGEN: return (o2Final() / AIR_O2_PERCENT * SIM_O2_CELL_AIR_MV + noise(0.01f)) / 1000.0f;
        case ADC_CH_HELIUM: return (o2AtA0() / AIR_O2_PERCENT * SIM_HE_CELL_AIR_MV + noise(0.01f)) / 1000.0f;
        case ADC_CH_BANK: return pressureAtAdc(bank_psi + noise(3.0f));
        case ADC_CH_FILL: return pressureAtAdc(fill_psi + noise(3.0f));
    }
    return 0.0f;
}
}  // namespace

float Adafruit_ADS1115::fullScale() const {
    switch (gain_) {
        case GAIN_TWOTHIRDS: return 6.144f;
        case GAIN_ONE: return 4.096f;
        case GAIN_TWO: return 2.048f;
        case GAIN_FOUR: return 1.024f;
        case GAIN_EIGHT: return 0.512f;
        case GAIN_SIXTEEN: return 0.256f;
    }
    return 6.144f;
}

void Adafruit_ADS1115::startADCReading(uint16_t mux, bool) {
    const int channel = (mux - ADS1X15_REG_CONFIG_MUX_SINGLE_0) >> 12;
    const float counts = channelVolts(channel) / fullScale() * 32768.0f;
    last_ = (int16_t)std::fmax(-32768.0f, std::fmin(32767.0f, std::round(counts)));
}

float Adafruit_ADS1115::computeVolts(int16_t counts) { return counts * fullScale() / 32768.0f; }

// Test hook: the true final mix (0 = O2 %, 1 = He %), independent of the cells.
#include <emscripten/emscripten.h>
extern "C" EMSCRIPTEN_KEEPALIVE float sim_gas(int which) {
    return which == 0 ? o2Final() : he_share * (1.0f - o2_added) * 100.0f;
}

// Test hook: scale the O2 valve's flow, as if the rig changed under a learned controller.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_set_o2_flow(float flow) { o2_flow = flow; }
extern "C" EMSCRIPTEN_KEEPALIVE void sim_set_o2_leak(float leak) { o2_leak = leak; }
