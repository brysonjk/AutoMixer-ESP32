#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

#include <Arduino.h>
#include "sensors.h"
#include "valve_control.h"
#include "gui.h"

// Everything the web page shows, copied out by the main loop.
struct WebSnapshot {
    bool sensors;
    bool remote;
    bool estop;
    bool he_on;
    bool pressure;
    bool bar;
    float o2_pct, o2_mv, o2_target, o2_valve;
    float he_pct, he_mv, he_target, he_valve;
    float bank, fill;       // already in the unit's pressure units
    char pin[7];
    char compressor[16];
    char status[24];
    char detail[112];        // the status panel's second line, e.g. an E-STOP's reason
    // Filling mode, and the readouts the unit shows for it (empty when it shows none).
    bool fill_mode;
    char bank_rate[24], bank_gap[32], bank_eq[40];
    char fill_rate[24], fill_flow[32], fill_eta[32], cylinder[64];
    // Blending mode's bank card.
    char bank_size[24], bank_flow[32];
};

// Serves a live dashboard on port 80. View-only by default; changing a target needs
// remote control armed at the unit plus its PIN. The E-STOP needs neither.
//
// The server runs in its own task, so a slow or stalled client (the server waits up to
// 5 s per request line) can never hold up the control loop or the touchscreen E-STOP.
// That task never touches the GUI, sensors or valves: it reads a snapshot the main loop
// publishes, and hands changes back as commands the main loop applies.
class WebDashboard {
public:
    WebDashboard(OxygenSensor* sensors, ValveController* valves, FillStationGUI* gui);
    void begin();
    // Main loop: applies queued commands and refreshes the snapshot.
    void loop();

private:
    OxygenSensor* sensors;
    ValveController* valves;
    FillStationGUI* gui;
    uint32_t last_publish_ms;

    // Web task only.
    uint8_t failed_pins;
    uint32_t locked_until_ms;

    void publish();
    WebSnapshot snapshot();
    static void task(void* arg);
    void handleState();
    void handleTarget();
    void handleEstop();
    bool locked();
};

#endif
