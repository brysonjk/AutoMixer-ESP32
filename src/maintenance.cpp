#include "maintenance.h"
#include <Preferences.h>

// Running time is saved this often while the compressor runs, and when it stops: a
// power cut loses at most this much, and flash sees a write every few minutes at most.
static const uint32_t SAVE_EVERY_MS = 5 * 60 * 1000;
static const uint16_t MAX_LIMIT_H = 9999;

static const char* const SINCE_KEYS[MAINT_COUNT] = {"filter_s", "oil_s"};
static const char* const LIMIT_KEYS[MAINT_COUNT] = {"filter_lim", "oil_lim"};

const char* Maintenance::name(MaintCounter c) {
    return c == MAINT_FILTER ? "Filter" : "Oil";
}

void Maintenance::begin() {
    Preferences prefs;
    prefs.begin("maint", true);
    // isKey() first: a get on a missing key logs an NVS error on every boot.
    if (prefs.isKey("total_s")) total_s = prefs.getUInt("total_s", 0);
    for (int i = 0; i < MAINT_COUNT; i++) {
        if (prefs.isKey(SINCE_KEYS[i])) since_s[i] = prefs.getUInt(SINCE_KEYS[i], 0);
        if (prefs.isKey(LIMIT_KEYS[i])) limit_h[i] = prefs.getUShort(LIMIT_KEYS[i], 0);
    }
    prefs.end();
    last_ms = last_save_ms = millis();
    Serial.printf("maintenance: compressor %.1f h total, filter %.1f h (limit %u), oil %.1f h (limit %u)\n",
                  totalHours(), hours(MAINT_FILTER), limit_h[MAINT_FILTER], hours(MAINT_OIL),
                  limit_h[MAINT_OIL]);
}

void Maintenance::tick(bool compressor_running) {
    const uint32_t now = millis();
    if (compressor_running) {
        carry_ms += now - last_ms;
        while (carry_ms >= 1000) {
            carry_ms -= 1000;
            total_s++;
            for (int i = 0; i < MAINT_COUNT; i++) since_s[i]++;
            dirty = true;
        }
    }
    last_ms = now;

    const bool stopped = was_running && !compressor_running;
    was_running = compressor_running;
    if (dirty && (stopped || now - last_save_ms >= SAVE_EVERY_MS)) save();
}

void Maintenance::setLimitHours(MaintCounter c, int hours) {
    limit_h[c] = (uint16_t)constrain(hours, 0, (int)MAX_LIMIT_H);
    dirty = true;
}

void Maintenance::reset(MaintCounter c) {
    Serial.printf("maintenance: %s counter reset at %.1f h\n", name(c), hours(c));
    since_s[c] = 0;
    dirty = true;
    save();
}

void Maintenance::save() {
    Preferences prefs;
    prefs.begin("maint", false);
    prefs.putUInt("total_s", total_s);
    for (int i = 0; i < MAINT_COUNT; i++) {
        prefs.putUInt(SINCE_KEYS[i], since_s[i]);
        prefs.putUShort(LIMIT_KEYS[i], limit_h[i]);
    }
    prefs.end();
    dirty = false;
    last_save_ms = millis();
}
