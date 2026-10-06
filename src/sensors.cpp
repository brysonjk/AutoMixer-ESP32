#include "sensors.h"
#include <Wire.h>
#include <Preferences.h>

OxygenSensor::OxygenSensor()
    : available(false), oxygen_percent(NAN), helium_percent(NAN), he_cell_he_percent(NAN),
      he_cell_o2_percent(NAN),
      oxygen_mv(NAN), helium_mv(NAN),
      o2_air_mv(DEFAULT_CELL_AIR_MV), he_air_mv(DEFAULT_CELL_AIR_MV), o2_calibrated(false),
      he_calibrated(false), he_cell_used(true),
      cal_active(false), cal_start_ms(0), cal_last_sample_ms(0), cal_result(CAL_NONE),
      cal_measured_o2(0.0f), cal_measured_he(0.0f), adc_ok(true), last_update_ms(0) {
    for (int i = 0; i < 2; i++) {
        pressure_psi[i] = NAN;
        pressure_volts[i] = NAN;
    }
    resetPressureCal(PRESSURE_BANK);
    resetPressureCal(PRESSURE_FILL);
}

static const uint8_t PRESSURE_ADC_CH[2] = {ADC_CH_BANK, ADC_CH_FILL};
static const char* PRESSURE_KEY_ZERO[2] = {"bz", "fz"};
static const char* PRESSURE_KEY_SLOPE[2] = {"bs", "fs"};
static const float NOMINAL_PSI_PER_V = PRESSURE_PSI_FULL / (PRESSURE_V_FULL - PRESSURE_V_ZERO);

void OxygenSensor::resetPressureCal(PressureChannel p) {
    pressure_cal[p] = {PRESSURE_V_ZERO, NOMINAL_PSI_PER_V, false, false};
}

void OxygenSensor::clearPressureCal(PressureChannel p) {
    resetPressureCal(p);
    savePressureCal(p);
    Serial.printf("pressure %u calibration cleared\n", (unsigned)p);
}

void OxygenSensor::loadPressureCal() {
    Preferences prefs;
    prefs.begin("pcal", false);
    for (int i = 0; i < 2; i++) {
        PressureCal& c = pressure_cal[i];
        if (prefs.isKey(PRESSURE_KEY_ZERO[i])) {
            c.zero_v = prefs.getFloat(PRESSURE_KEY_ZERO[i], PRESSURE_V_ZERO);
            c.zero_set = true;
        }
        if (prefs.isKey(PRESSURE_KEY_SLOPE[i])) {
            c.psi_per_v = prefs.getFloat(PRESSURE_KEY_SLOPE[i], NOMINAL_PSI_PER_V);
            c.span_set = true;
        }
    }
    prefs.end();
}

// Saves what has been set and removes what hasn't, so a reset sticks across a restart.
void OxygenSensor::savePressureCal(PressureChannel p) {
    const PressureCal& c = pressure_cal[p];
    Preferences prefs;
    prefs.begin("pcal", false);
    if (c.zero_set) prefs.putFloat(PRESSURE_KEY_ZERO[p], c.zero_v);
    else if (prefs.isKey(PRESSURE_KEY_ZERO[p])) prefs.remove(PRESSURE_KEY_ZERO[p]);
    if (c.span_set) prefs.putFloat(PRESSURE_KEY_SLOPE[p], c.psi_per_v);
    else if (prefs.isKey(PRESSURE_KEY_SLOPE[p])) prefs.remove(PRESSURE_KEY_SLOPE[p]);
    prefs.end();
}

PressureCalResult OxygenSensor::averagePressureVolts(PressureChannel p, float* volts) {
    *volts = NAN;
    if (!available) return PCAL_NO_READING;
    float sum = 0.0f, lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < PCAL_SAMPLES; i++) {
        const float v = readPressureVolts(PRESSURE_ADC_CH[p]);
        if (isnan(v)) return PCAL_NO_READING;
        sum += v;
        lo = min(lo, v);
        hi = max(hi, v);
    }
    *volts = sum / PCAL_SAMPLES;
    return hi - lo > PCAL_MAX_SPREAD_V ? PCAL_UNSTABLE : PCAL_OK;
}

PressureCalResult OxygenSensor::calibratePressureZero(PressureChannel p, float* volts) {
    const PressureCalResult r = averagePressureVolts(p, volts);
    if (r != PCAL_OK) return r;
    if (fabsf(*volts - PRESSURE_V_ZERO) > PCAL_ZERO_TOLERANCE_V) return PCAL_NOT_ZERO;
    pressure_cal[p].zero_v = *volts;
    pressure_cal[p].zero_set = true;
    savePressureCal(p);
    Serial.printf("pressure %u zero set: %.4f V\n", (unsigned)p, *volts);
    return PCAL_OK;
}

PressureCalResult OxygenSensor::calibratePressureSpan(PressureChannel p, float reference_psi,
                                                      float* volts) {
    const PressureCalResult r = averagePressureVolts(p, volts);
    if (r != PCAL_OK) return r;
    const float rise = *volts - pressure_cal[p].zero_v;
    if (!(reference_psi > 0.0f) || rise < PCAL_MIN_SPAN_V) return PCAL_BAD_SPAN;
    const float slope = reference_psi / rise;
    if (fabsf(slope / NOMINAL_PSI_PER_V - 1.0f) > PCAL_SPAN_TOLERANCE) return PCAL_BAD_SPAN;
    pressure_cal[p].psi_per_v = slope;
    pressure_cal[p].span_set = true;
    savePressureCal(p);
    Serial.printf("pressure %u span set: %.1f PSI/V at %.0f PSI\n", (unsigned)p, slope,
                  reference_psi);
    return PCAL_OK;
}

void OxygenSensor::loadCalibration() {
    Preferences prefs;
    prefs.begin("cal", false);
    // Each cell separately: a Nitrox-only unit calibrates just the O2 cell.
    o2_calibrated = prefs.isKey("o2_air");
    he_calibrated = prefs.isKey("he_air");
    if (o2_calibrated) o2_air_mv = prefs.getFloat("o2_air", DEFAULT_CELL_AIR_MV);
    if (he_calibrated) he_air_mv = prefs.getFloat("he_air", DEFAULT_CELL_AIR_MV);
    prefs.end();
    Serial.printf("O2 cell A1 %s, A0 %s\n",
                  o2_calibrated ? String(o2_air_mv, 2).c_str() : "NOT calibrated",
                  he_calibrated ? String(he_air_mv, 2).c_str() : "NOT calibrated");
}

bool OxygenSensor::begin() {
    loadCalibration();
    loadPressureCal();
    Wire.begin(ADS1115_SDA, ADS1115_SCL);

    if (!ads.begin()) {
        Serial.println("ADS1115 not found - running without sensors");
        available = false;
        return false;
    }
    available = true;

    Serial.println("ADS1115 initialized successfully");
    return true;
}

// One step of an exponential moving average with time constant tau_s over dt_s. A
// failed reading (NAN) passes straight through, so a fault shows at once instead of the
// last good value lingering; the next good reading then seeds the filter afresh, as
// does the first reading after boot.
static float smooth(float previous, float reading, float dt_s, float tau_s) {
    if (isnan(reading) || isnan(previous)) return reading;
    const float alpha = 1.0f - expf(-dt_s / tau_s);
    return previous + alpha * (reading - previous);
}

void OxygenSensor::update() {
    if (!available) return;

    const uint32_t now = millis();
    // First pass: dt of 0 is harmless, since the filters are still NAN and seed directly.
    const float dt = last_update_ms == 0 ? 0.0f : (now - last_update_ms) / 1000.0f;
    last_update_ms = now;

    oxygen_mv = smooth(oxygen_mv, readCellMillivolts(ADC_CH_OXYGEN), dt, O2_SMOOTHING_S);
    helium_mv = smooth(helium_mv, readCellMillivolts(ADC_CH_HELIUM), dt, O2_SMOOTHING_S);

    oxygen_percent = cellO2Percent(oxygen_mv, o2_air_mv);
    he_cell_o2_percent = cellO2Percent(helium_mv, he_air_mv);

    // Helium isn't measured directly: it's whatever displaced the air's oxygen at A0.
    if (isnan(he_cell_o2_percent)) {
        he_cell_he_percent = NAN;
    } else {
        const float he = (AIR_O2_PERCENT - he_cell_o2_percent) / AIR_O2_PERCENT * 100.0f;
        he_cell_he_percent = constrain(he, 0.0f, 100.0f);
    }
    helium_percent = heliumAfterO2(oxygen_percent);

    for (int i = 0; i < 2; i++) {
        const PressureCal& c = pressure_cal[i];
        pressure_volts[i] = smooth(pressure_volts[i], readPressureVolts(PRESSURE_ADC_CH[i]), dt,
                                   PRESSURE_SMOOTHING_S);
        const float psi = (pressure_volts[i] - c.zero_v) * c.psi_per_v;
        // Small offsets below the zero point are sensor tolerance, not negative pressure.
        pressure_psi[i] = isnan(psi) ? NAN : max(psi, 0.0f);
    }
}

float OxygenSensor::heliumAfterO2(float final_o2_percent) {
    if (isnan(he_cell_he_percent) || isnan(final_o2_percent)) return NAN;
    // Guards the division; a real A0 cell never reads anywhere near pure oxygen.
    if (he_cell_o2_percent >= 99.0f) return NAN;
    const float he = he_cell_he_percent * (100.0f - final_o2_percent) /
                     (100.0f - he_cell_o2_percent);
    return constrain(he, 0.0f, 100.0f);
}

// One single-shot conversion, or NAN if it doesn't complete. Not the library's
// readADC_SingleEnded(): that spins on the conversion-done bit with no timeout, and a
// failed I2C read leaves that bit clear, so one loose wire to the ADS1115 would freeze
// the whole unit with the valves held wherever they were. A conversion takes ~8 ms at
// the default 128 SPS.
static const uint32_t ADC_TIMEOUT_MS = 25;

float OxygenSensor::readVolts(uint8_t channel, adsGain_t gain) {
    ads.setGain(gain);
    ads.startADCReading(MUX_BY_CHANNEL[channel], false);
    const uint32_t start = millis();
    bool done;
    while (!(done = ads.conversionComplete()) && millis() - start < ADC_TIMEOUT_MS) {
    }
    if (done != adc_ok) {
        adc_ok = done;
        Serial.println(done ? "ADS1115 reading again" : "ADS1115 read timed out - check wiring");
    }
    return done ? ads.computeVolts(ads.getLastConversionResults()) : NAN;
}

// O2 cells put out only ~10 mV in air, so they're read on the ADS1115's +/-0.256 V range
// (7.8 uV per count). At the +/-4.096 V range one count would be ~0.26% O2, and helium,
// derived from the O2 deficit, would move in ~1.25% steps.
float OxygenSensor::readCellMillivolts(uint8_t channel) {
    return readVolts(channel, GAIN_SIXTEEN) * 1000.0f;
}

float OxygenSensor::cellO2Percent(float mv, float air_mv) {
    // NAN compares false with everything, so it's checked explicitly.
    if (isnan(mv) || mv < O2_CELL_FAULT_MV) return NAN;
    return constrain(mv / air_mv * AIR_O2_PERCENT, 0.0f, 100.0f);
}

// The transducer's own output voltage, or NAN if the ADC read failed or the signal is
// outside what a working transducer can produce (a broken wire or a short).
float OxygenSensor::readPressureVolts(uint8_t channel) {
    // Transducers need the +/-4.096 V range: they reach ~3 V after the divider.
    const float sensor_volts = readVolts(channel, GAIN_ONE) / PRESSURE_DIVIDER;
    if (isnan(sensor_volts) || sensor_volts < PRESSURE_FAULT_LOW_V ||
        sensor_volts > PRESSURE_FAULT_HIGH_V) {
        return NAN;
    }
    return sensor_volts;
}

bool OxygenSensor::startCalibration() {
    if (!available) {
        cal_result = CAL_NO_ADC;
        return false;
    }
    for (int i = 0; i < 2; i++) {
        cal_o2_sum[i] = 0.0f;
        cal_he_sum[i] = 0.0f;
        cal_n[i] = 0;
    }
    cal_o2_sq = cal_he_sq = 0.0;
    cal_o2_min = cal_he_min = INFINITY;
    cal_o2_max = cal_he_max = -INFINITY;
    cal_noise_o2 = cal_noise_he = 0.0f;
    cal_start_ms = millis();
    cal_last_sample_ms = cal_start_ms - CAL_SAMPLE_MS;
    cal_active = true;
    Serial.println("calibration started");
    return true;
}

uint8_t OxygenSensor::calibrationSecondsLeft() {
    if (!cal_active) return 0;
    const uint32_t elapsed = millis() - cal_start_ms;
    return elapsed >= CAL_DURATION_MS ? 0 : (CAL_DURATION_MS - elapsed + 999) / 1000;
}

void OxygenSensor::calibrationTick() {
    if (!cal_active) return;
    const uint32_t now = millis();

    if (now - cal_start_ms >= CAL_DURATION_MS) {
        finishCalibration();
        return;
    }
    if (now - cal_last_sample_ms < CAL_SAMPLE_MS) return;
    cal_last_sample_ms = now;

    // Split the window in halves so drift shows up as a difference between them.
    const int half = (now - cal_start_ms) < CAL_DURATION_MS / 2 ? 0 : 1;
    const float o2 = readCellMillivolts(ADC_CH_OXYGEN);
    float he = readCellMillivolts(ADC_CH_HELIUM);
    // A failed read ends the run: NAN in the sums would pass every range and drift check
    // (all comparisons with NAN are false) and be saved as the calibration.
    if (isnan(o2) || (he_cell_used && isnan(he))) {
        cal_active = false;
        cal_result = CAL_NO_ADC;
        Serial.println("calibration aborted: ADS1115 read failed");
        return;
    }
    if (isnan(he)) he = 0.0f;   // no helium cell in Nitrox only; its sum is unused
    cal_o2_sum[half] += o2;
    cal_he_sum[half] += he;
    cal_n[half]++;
    cal_o2_sq += (double)o2 * o2;
    cal_he_sq += (double)he * he;
    cal_o2_min = min(cal_o2_min, o2);
    cal_o2_max = max(cal_o2_max, o2);
    cal_he_min = min(cal_he_min, he);
    cal_he_max = max(cal_he_max, he);
}

static bool drifted(float a, float b) {
    const float mean = (a + b) / 2.0f;
    return mean <= 0.0f || fabsf(a - b) / mean > CAL_MAX_DRIFT;
}

void OxygenSensor::finishCalibration() {
    cal_active = false;
    if (cal_n[0] == 0 || cal_n[1] == 0) {
        cal_result = CAL_UNSTABLE;
        return;
    }

    const float o2_a = cal_o2_sum[0] / cal_n[0], o2_b = cal_o2_sum[1] / cal_n[1];
    const float he_a = cal_he_sum[0] / cal_n[0], he_b = cal_he_sum[1] / cal_n[1];
    cal_measured_o2 = (cal_o2_sum[0] + cal_o2_sum[1]) / (cal_n[0] + cal_n[1]);
    cal_measured_he = (cal_he_sum[0] + cal_he_sum[1]) / (cal_n[0] + cal_n[1]);

    const int n = cal_n[0] + cal_n[1];
    auto std_dev = [n](double sq, float mean) {
        const double var = sq / n - (double)mean * mean;
        return var > 0.0 ? (float)sqrt(var) : 0.0f;
    };
    cal_noise_o2 = std_dev(cal_o2_sq, cal_measured_o2);
    cal_noise_he = std_dev(cal_he_sq, cal_measured_he);

    Serial.printf("calibration: A1 %.3f/%.3f mV sd %.4f p-p %.4f, A0 %.3f/%.3f mV sd %.4f p-p %.4f\n",
                  o2_a, o2_b, cal_noise_o2, cal_o2_max - cal_o2_min, he_a, he_b, cal_noise_he,
                  cal_he_max - cal_he_min);

    // Without a helium cell only A1 is judged; A0 is whatever an empty input reads.
    const bool he = he_cell_used;
    auto out_of_range = [](float mv) { return mv < CAL_MIN_AIR_MV || mv > CAL_MAX_AIR_MV; };
    if (out_of_range(cal_measured_o2) || (he && out_of_range(cal_measured_he))) {
        cal_result = CAL_OUT_OF_RANGE;
        return;
    }
    if (drifted(o2_a, o2_b) || (he && drifted(he_a, he_b))) {
        cal_result = CAL_UNSTABLE;
        return;
    }
    // Means are within 5-25 mV here (range check above), so the ratios are safe.
    auto noisy = [](float sd, float lo, float hi, float mean) {
        return sd / mean > CAL_MAX_NOISE || (hi - lo) / mean > CAL_MAX_SPREAD;
    };
    if (noisy(cal_noise_o2, cal_o2_min, cal_o2_max, cal_measured_o2) ||
        (he && noisy(cal_noise_he, cal_he_min, cal_he_max, cal_measured_he))) {
        cal_result = CAL_NOISY;
        return;
    }

    Preferences prefs;
    prefs.begin("cal", false);
    o2_air_mv = cal_measured_o2;
    o2_calibrated = true;
    prefs.putFloat("o2_air", o2_air_mv);
    if (he) {
        he_air_mv = cal_measured_he;
        he_calibrated = true;
        prefs.putFloat("he_air", he_air_mv);
    }
    prefs.end();
    cal_result = CAL_OK;
    Serial.printf("calibration saved: A1 %.2f mV, A0 %.2f mV\n", o2_air_mv, he_air_mv);
}
