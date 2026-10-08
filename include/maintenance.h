#ifndef MAINTENANCE_H
#define MAINTENANCE_H

#include <Arduino.h>

// Compressor hour meter and maintenance reminders, counted while the compressor-running
// input is on. A lifetime total that is never reset, plus resettable counters, each with
// a reminder limit in hours (0 = no reminder). Stored in NVS namespace "maint".
enum MaintCounter : uint8_t { MAINT_FILTER = 0, MAINT_OIL = 1, MAINT_COUNT = 2 };

class Maintenance {
public:
    void begin();
    // Call every loop pass with the debounced compressor state.
    void tick(bool compressor_running);

    float totalHours() const { return total_s / 3600.0f; }
    float hours(MaintCounter c) const { return since_s[c] / 3600.0f; }
    uint16_t limitHours(MaintCounter c) const { return limit_h[c]; }
    bool due(MaintCounter c) const { return limit_h[c] > 0 && since_s[c] >= limit_h[c] * 3600u; }
    static const char* name(MaintCounter c);

    // Changes take effect at once; the limit is saved by save(), so a held +/- button
    // writes flash once on release rather than on every step.
    void setLimitHours(MaintCounter c, int hours);
    void reset(MaintCounter c);
    void save();

private:
    uint32_t total_s = 0;
    uint32_t since_s[MAINT_COUNT] = {0, 0};
    uint16_t limit_h[MAINT_COUNT] = {0, 0};
    uint32_t last_ms = 0;
    uint32_t carry_ms = 0;
    uint32_t last_save_ms = 0;
    bool was_running = false;
    bool dirty = false;
};

#endif
