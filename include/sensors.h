#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Adafruit_ADS1X15.h>

// ADS1115 I2C pins for ESP32-S3
#define ADS1115_SDA 19
#define ADS1115_SCL 20

// ADS1115 channels
#define ADC_CH_HELIUM 0   // O2 cell after the helium injection
#define ADC_CH_OXYGEN 1   // O2 cell on the final mix
#define ADC_CH_BANK   2
#define ADC_CH_FILL   3

// Oxygen cells. Both channels carry galvanic O2 cells. Helium is injected first and
// oxygen second. A0 sits between the two, where helium shows up as missing oxygen:
//   He% at A0 = (AIR_O2_PERCENT - O2 at A0) / AIR_O2_PERCENT * 100
// A1 reads the final mix, after the oxygen has diluted that helium. See heliumAfterO2().
// A galvanic cell's output is linear through zero, so each is calibrated by its mV
// reading in air. The Calibrate button measures and saves these; the defaults below
// are only used until the first calibration.
#define AIR_O2_PERCENT          20.9f
#define DEFAULT_CELL_AIR_MV     10.0f

// Calibration: average both cells over a fixed window, then reject the result if the
// cells were still drifting (a cell moved from a mix into air takes 30 s+ to settle) or
// read outside what a healthy cell produces in air.
#define CAL_DURATION_MS         10000
#define CAL_SAMPLE_MS           200
#define CAL_MAX_DRIFT           0.01f   // first half vs second half of the window
// Noise: drift compares the two halves' averages, so a reading bouncing up and down
// around a steady mean would pass it. These catch that, relative to the cell's reading:
// the standard deviation (steady noise) and the full spread (spikes). For a ~10 mV cell,
// 1% is 0.1 mV, about 0.2% O2.
#define CAL_MAX_NOISE           0.01f   // standard deviation
#define CAL_MAX_SPREAD          0.04f   // highest minus lowest reading
#define CAL_MIN_AIR_MV          5.0f
#define CAL_MAX_AIR_MV          25.0f

// Below this a cell is treated as disconnected or dead. Left unchecked, an unplugged A0
// cell reads 0% O2 and so 100% helium, and an unplugged A1 cell would drive the O2
// valve fully open. A live cell in anything breathable stays well above it.
#define O2_CELL_FAULT_MV    1.0f

// Exponential moving average on the raw readings (cell mV, transducer volts), applied
// before they become percentages or PSI, so the display, web page and control loop all
// see the smoothed value. Set as a time constant rather than a per-sample factor so it
// behaves the same whatever the read rate: after one time constant a step change is 63%
// through, after three 95%. A galvanic cell itself takes several seconds to respond, so
// these add little lag; raise them for steadier readings, lower them for faster ones.
// Calibration reads the sensors raw: it averages on its own and must see drift.
#define O2_SMOOTHING_S       1.5f
#define PRESSURE_SMOOTHING_S 1.0f

// Pressure transducers, both 0-5000 PSI gauge, 0.5-4.5 V ratiometric to a 5 V (+/-5%)
// supply, so one set of constants serves both:
//   TE M7139-05KPG-5-00000 (M7100): +/-0.25% accuracy, Packard connector.
//   TE M3031-000005-05KPG (MSP300): +/-1% span accuracy, zero and span each +/-2% out of
//     the box, so calibrate it; 2 ft cable (red +5 V, black common, white output, green
//     unused). Not oxygen cleaned.
// A transducer with a different range or output needs these changed (or a per-channel
// copy). The output is fed through a
// resistor divider, because the ADS1115 runs from 3.3 V and must never see more than
// VDD + 0.3 V on an input. PRESSURE_DIVIDER is volts-at-ADC / volts-at-sensor.
#define PRESSURE_V_ZERO   0.5f     // sensor output at 0 PSI
#define PRESSURE_V_FULL   4.5f     // sensor output at PRESSURE_PSI_FULL
#define PRESSURE_PSI_FULL 5000.0f
#define PRESSURE_DIVIDER  (20.0f / 30.0f)   // 10k top, 20k bottom: 4.5 V -> 3.0 V

// A 0.5-4.5 V transducer never outputs near 0 V or its supply rail, so readings
// outside this window mean a broken wire or a short, not a pressure.
#define PRESSURE_FAULT_LOW_V  0.25f
#define PRESSURE_FAULT_HIGH_V 4.75f

// Two-point pressure calibration, per transducer: zero with it vented to air, span at a
// known pressure read off a reference gauge. It absorbs the transducer's own offset and
// span error, the 5 V excitation's error (the output is ratiometric) and the divider's
// resistor tolerance. The checks below catch a transducer that isn't actually vented,
// a reference entered in the wrong units, or a pressure still settling.
#define PCAL_SAMPLES          16
#define PCAL_ZERO_TOLERANCE_V 0.15f   // zero must read within this of PRESSURE_V_ZERO
#define PCAL_SPAN_TOLERANCE   0.20f   // span slope within +/-20% of the nominal one
#define PCAL_MIN_SPAN_V       0.4f    // reference at least ~10% of full scale
#define PCAL_MAX_SPREAD_V     0.02f   // samples must agree to within this (~25 PSI)

enum PressureChannel { PRESSURE_BANK = 0, PRESSURE_FILL = 1 };

enum PressureCalResult {
    PCAL_OK,
    PCAL_NO_READING,   // ADS1115 absent, or the signal is out of range (wiring fault)
    PCAL_UNSTABLE,     // pressure still changing
    PCAL_NOT_ZERO,     // doesn't read like 0 PSI: not vented?
    PCAL_BAD_SPAN,     // reference too low, or the slope is implausible (wrong units?)
};

struct PressureCal {
    float zero_v;       // sensor output at 0 PSI
    float psi_per_v;
    bool zero_set;      // false: still the datasheet value
    bool span_set;
};

enum CalResult {
    CAL_NONE,          // never run this boot
    CAL_OK,
    CAL_NO_ADC,
    CAL_UNSTABLE,      // readings still drifting
    CAL_NOISY,         // readings bouncing around (wiring, grounding, a failing cell)
    CAL_OUT_OF_RANGE,  // a cell's reading isn't plausible for air
};

class OxygenSensor {
public:
    OxygenSensor();
    bool begin();
    void update();          // Read all sensors

    // Air calibration. Call calibrationTick() every loop pass; it paces its own sampling.
    bool startCalibration();
    void calibrationTick();
    bool calibrating() { return cal_active; }
    uint8_t calibrationSecondsLeft();
    CalResult lastCalibration() { return cal_result; }
    // Calibrated for the current gas mode: the O2 cell always, the helium cell only
    // while it's in use (Trimix).
    bool isCalibrated() { return o2_calibrated && (!he_cell_used || he_calibrated); }

    // Nitrox-only blending has no helium cell: calibration then checks and saves only
    // the O2 cell, and an absent or uncalibrated helium cell doesn't block the valves.
    void setHeliumCellUsed(bool used) { he_cell_used = used; }
    bool heliumCellUsed() { return he_cell_used; }
    float getO2CellAirMV() { return o2_air_mv; }
    float getHeCellAirMV() { return he_air_mv; }
    // Averages from the last run, including a rejected one, for the result message.
    float calMeasuredO2MV() { return cal_measured_o2; }
    float calMeasuredHeMV() { return cal_measured_he; }
    // Standard deviation of each cell's readings over the last run, mV.
    float calNoiseO2MV() { return cal_noise_o2; }
    float calNoiseHeMV() { return cal_noise_he; }

    // Getters. Percentages are NAN when the ADC is absent or a cell reads as faulted.
    bool isAvailable() { return available; }
    float getOxygenPercent() { return oxygen_percent; }
    float getHeliumPercent() { return helium_percent; }         // in the final mix
    float getHeCellO2Percent() { return he_cell_o2_percent; }   // raw O2 at the A0 cell

    // Helium in the final mix, given its O2 percentage. Oxygen added after A0 dilutes
    // everything else equally, so the helium's share of the non-oxygen gas is unchanged:
    //   He_final = He_A0 * (100 - O2_final) / (100 - O2_A0)
    // NAN if either input is.
    float heliumAfterO2(float final_o2_percent);
    float getOxygenMillivolts() { return oxygen_mv; }
    float getHeliumMillivolts() { return helium_mv; }

    // PSI, or NAN when the sensor is absent or its signal is out of range.
    float getBankPSI() { return pressure_psi[PRESSURE_BANK]; }
    float getFillPSI() { return pressure_psi[PRESSURE_FILL]; }
    float getPressurePSI(PressureChannel p) { return pressure_psi[p]; }
    // Transducer output voltage (before the divider), NAN if out of range.
    float getPressureVolts(PressureChannel p) { return pressure_volts[p]; }

    // Pressure calibration, saved to NVS namespace "pcal". Each reads PCAL_SAMPLES
    // conversions (~150 ms) and reports the averaged transducer voltage through volts.
    PressureCalResult calibratePressureZero(PressureChannel p, float* volts);
    PressureCalResult calibratePressureSpan(PressureChannel p, float reference_psi,
                                            float* volts);
    // Back to the datasheet scaling, saved.
    void clearPressureCal(PressureChannel p);
    PressureCal getPressureCal(PressureChannel p) { return pressure_cal[p]; }

private:
    Adafruit_ADS1115 ads;
    bool available;
    float oxygen_percent;
    float helium_percent;
    float he_cell_he_percent;   // helium at A0, before the oxygen is added
    float he_cell_o2_percent;
    float oxygen_mv;
    float helium_mv;
    float pressure_psi[2];
    float pressure_volts[2];
    PressureCal pressure_cal[2];

    // Per-cell air calibration, persisted in NVS.
    float o2_air_mv;
    float he_air_mv;
    bool o2_calibrated;
    bool he_calibrated;
    bool he_cell_used;

    bool cal_active;
    uint32_t cal_start_ms;
    uint32_t cal_last_sample_ms;
    float cal_o2_sum[2];
    float cal_he_sum[2];
    uint16_t cal_n[2];
    CalResult cal_result;
    float cal_measured_o2;
    float cal_measured_he;
    // Whole-window noise statistics. Squares summed in double: the variance is a tiny
    // difference of two large numbers, which float would lose.
    double cal_o2_sq, cal_he_sq;
    float cal_o2_min, cal_o2_max, cal_he_min, cal_he_max;
    float cal_noise_o2, cal_noise_he;

    void finishCalibration();
    void loadCalibration();
    void loadPressureCal();
    void savePressureCal(PressureChannel p);
    void resetPressureCal(PressureChannel p);
    float readPressureVolts(uint8_t channel);
    PressureCalResult averagePressureVolts(PressureChannel p, float* volts);

    bool adc_ok;    // last read completed; for logging the change only
    uint32_t last_update_ms;   // for the smoothing filter's time step

    float readVolts(uint8_t channel, adsGain_t gain);
    float readCellMillivolts(uint8_t channel);
    static float cellO2Percent(float mv, float air_mv);
};

#endif
