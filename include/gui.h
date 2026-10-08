#ifndef GUI_H
#define GUI_H

#include <lvgl.h>
#include "sensors.h"
#include "valve_control.h"
#include "wifi_manager.h"
#include "maintenance.h"


// A gas preset, O2/He in percent.
struct GasPreset {
    uint8_t o2;
    uint8_t he;
};

// Enough for four full rows in the presets grid without scrolling.
#define MAX_PRESETS 16

#define PSI_PER_BAR 14.5038f

// A cylinder or bank bottle, as it's sold: rated volume at a working pressure. Imperial
// specs are rated cu ft at psi; metric ones are water capacity in litres at bar.
struct CylinderSpec {
    char name[12];
    float rated;   // cu ft (imperial) or water litres (metric)
    float wp;      // working pressure, psi (imperial) or bar (metric)
    bool metric;
};

// Straight-line trend of one pressure over the last 30 s, sampled once a second.
struct PressureTrend {
    static const int SIZE = 31;
    float t[SIZE], v[SIZE];
    int head = 0, n = 0;
    void add(float t_s, float psi);   // NAN clears the history
    float perMinute() const;          // NAN until there are 10 s of readings
};

struct KnobState {
    const char* name;
    uint8_t channel;    // ADS1115 channel feeding this loop
    float value;        // 0-100
    lv_obj_t* picker;   // roller wheel selecting the target
};

class FillStationGUI {
public:
    FillStationGUI(OxygenSensor* sensor, ValveController* valves, WifiManager* wifi);
    void init();
    void update();

    void setCompressorRunning(bool running);
    // Hour meter and maintenance reminders: shown on Setup > Maintenance and, when due,
    // on the status panel. Set before init().
    void setMaintenance(Maintenance* m) { maint = m; }
    // Set while main.cpp holds the O2 valve shut because the mix is over target.
    void setO2OverTarget(bool over) { o2_over_target = over; }
    void setIpAddress(const char* ip);

    bool emergencyStopped() { return estop_active; }
    // Filling mode: pressures, flow and time to full for a cylinder fill; blending is off
    // and main.cpp keeps both valves shut. Only offered while pressure sensors are fitted.
    bool fillMode() { return fill_mode && transducers_fitted; }
    void triggerEmergencyStop();

    float getOxygenTarget() { return o2_knob.value; }
    float getHeliumTarget() { return he_knob.value; }
    void setOxygenTarget(float value);
    void setHeliumTarget(float value);

    // Web dashboard access. Remote changes are allowed only while armed at the unit.
    bool remoteControlEnabled() { return remote_enabled; }
    const char* remotePin() { return remote_pin; }
    const char* compressorText();
    const char* systemStatusText();

    // Blender Setup choices, persisted in NVS namespace "blender".
    bool pressureInBar() { return units_bar; }
    bool heliumEnabled() { return helium_enabled; }        // false: Nitrox only
    bool transducersFitted() { return transducers_fitted; }

    // Setup > Valve test: while its page is open, main.cpp drives the chosen valve at
    // this raw opening (0-100) instead of running the control loops.
    bool valveTestActive();
    bool valveTestHelium() { return vt_helium; }
    float valveTestOpening() { return vt_opening; }

private:
    OxygenSensor* sensors;
    ValveController* valve_ctrl;
    WifiManager* wifi;

    lv_obj_t* screen;
    lv_obj_t* screen_menu;      // Setup: the list of settings pages
    lv_obj_t* screen_blender;
    lv_obj_t* screen_network;
    lv_obj_t* screen_pcal;      // pressure calibration
    lv_obj_t* screen_valvetest = nullptr;
    lv_obj_t* screen_maint = nullptr;

    // Blending / Filling mode, and what is being filled. Persisted in NVS namespace "fill".
    bool fill_mode = false;
    // Per pressure unit ([0] PSI, [1] BAR): index into that unit's preset list, one past
    // the end = custom. Defaults AL80 / 12 L and 444 cu ft / 50 L.
    uint8_t fill_sel[2] = {4, 3};
    uint8_t bank_sel[2] = {0, 0};
    uint8_t bank_count = 4;
    // Custom sizes, per pressure unit like the presets.
    CylinderSpec fill_custom[2] = {{"Custom", 80.0f, 3000.0f, false}, {"Custom", 12.0f, 232.0f, true}};
    CylinderSpec bank_custom[2] = {{"Custom", 444.0f, 4500.0f, false}, {"Custom", 50.0f, 300.0f, true}};
    lv_obj_t* btn_mode = nullptr;
    lv_obj_t* label_mode = nullptr;
    lv_obj_t* fill_panel = nullptr;
    lv_obj_t* fp_bank_psi = nullptr;
    lv_obj_t* fp_bank_rate = nullptr;
    lv_obj_t* fp_fill_psi = nullptr;
    lv_obj_t* fp_fill_rate = nullptr;
    lv_obj_t* fp_fill_flow = nullptr;
    lv_obj_t* fp_fill_eta = nullptr;
    lv_obj_t* fp_cyl_label = nullptr;
    lv_obj_t* fp_bank_flow = nullptr;     // Source card: how far above the fill
    lv_obj_t* fp_src_eq = nullptr;        // Source card: "Equalized" notice
    lv_obj_t* btn_bank = nullptr;          // Blending mode: bank size, where the Fill readout was
    lv_obj_t* label_bank_size = nullptr;
    lv_obj_t* label_bank_flow = nullptr;
    // Cylinder / bank picker page, rebuilt each time it opens.
    lv_obj_t* screen_cyl = nullptr;
    lv_obj_t* cyl_title = nullptr;
    lv_obj_t* cyl_body = nullptr;
    bool cyl_for_bank = false;
    lv_obj_t* cyl_tiles[16] = {};
    uint8_t cyl_tile_n = 0;
    lv_obj_t* cyl_size_val = nullptr;
    lv_obj_t* cyl_wp_val = nullptr;
    lv_obj_t* cyl_count_val = nullptr;
    PressureTrend bank_trend, fill_trend;
    uint32_t rate_sample_ms = 0;
    Maintenance* maint = nullptr;
    lv_obj_t* mt_total = nullptr;
    lv_obj_t* mt_hours[MAINT_COUNT] = {};
    lv_obj_t* mt_limit[MAINT_COUNT] = {};
    lv_obj_t* mt_state[MAINT_COUNT] = {};
    uint8_t mt_pending_reset = 0;

    // Valve test page.
    lv_obj_t* vt_gas = nullptr;
    lv_obj_t* vt_slider = nullptr;
    lv_obj_t* vt_value = nullptr;
    lv_obj_t* vt_readings = nullptr;
    lv_obj_t* vt_starts = nullptr;
    lv_obj_t* vt_status = nullptr;
    lv_obj_t* vt_learned = nullptr;
    bool o2_over_target = false;
    bool vt_helium = false;
    float vt_opening = 0.0f;
    uint32_t vt_touched_ms = 0;

    // Pressure calibration page, indexed by PressureChannel.
    lv_obj_t* pcal_now[2];      // live reading
    lv_obj_t* pcal_status[2];   // what's calibrated
    lv_obj_t* pcal_ref[2];      // reference pressure field
    lv_obj_t* pcal_ref_unit[2];
    lv_obj_t* keyboard_pcal;
    uint8_t pcal_pending;       // channel awaiting a zero/reset confirmation

    lv_obj_t* dropdown_networks;
    lv_obj_t* textarea_password;
    lv_obj_t* label_wifi_status;
    bool scan_pending;

    // Network: address settings. Fields are IP, mask, gateway, DNS.
    lv_obj_t* ip_mode;          // DHCP / Static IP
    lv_obj_t* ip_fields[4];
    lv_obj_t* keyboard;
    lv_obj_t* keyboard_target;

    bool units_bar;
    bool helium_enabled;
    bool transducers_fitted;

    // Main-screen columns. Each is a transparent container, so hiding the helium column
    // and re-centring the rest is a matter of moving four objects.
    lv_obj_t* col_left;
    lv_obj_t* col_o2;
    lv_obj_t* col_he;
    lv_obj_t* col_p;
    lv_coord_t bottom_margin_x;

    lv_obj_t* label_remote_badge;
    lv_obj_t* label_web_url;
    bool remote_enabled;
    char remote_pin[7];
    lv_obj_t* textarea_pin;     // Network page; edits and saves the PIN

    lv_obj_t* cal_dialog;       // the "Calibrating" box while a calibration runs
    lv_obj_t* preset_overlay;
    GasPreset presets[MAX_PRESETS];
    uint8_t preset_count;
    bool preset_edit_mode;
    uint8_t preset_pending_delete;
    // Presets shown in the grid, as indexes into presets[]: all of them, or only the
    // nitrox ones in Nitrox only mode.
    uint8_t preset_view[MAX_PRESETS];
    uint8_t preset_view_count;

    lv_obj_t* status_panel;
    lv_obj_t* status_dot;
    lv_obj_t* label_system_status;   // status headline, also sent to the web page
    lv_obj_t* label_status_detail;
    int status_tone;                 // colour last applied to the panel; -1 before the first

    lv_obj_t* label_o2_value;
    lv_obj_t* label_o2_mv;

    lv_obj_t* label_ip;
    lv_obj_t* btn_calibrate;
    lv_obj_t* btn_presets;
    lv_obj_t* label_he_value;
    lv_obj_t* label_he_mv;

    lv_obj_t* label_bank_psi;
    lv_obj_t* label_bank_unit;

    KnobState o2_knob;
    KnobState he_knob;

    // Set from the Setup screen. Each gas's sensor line then shows its ADC channel, raw
    // mV and valve state:  "P0 8.4mV PIO ON"
    bool diagnostics;

    // Latched until an operator confirms a resume; holds both valves closed meanwhile.
    bool estop_active;
    bool compressor_running;
    bool estop_dialog_open;
    lv_obj_t* estop_btn;
    lv_obj_t* estop_label;

    void createBanner();
    lv_obj_t* makeSubScreen(const char* title, lv_event_cb_t back_handler);
    void createSetupMenu();
    void createBlenderScreen();
    void createNetworkScreen();
    void createPressureCalScreen();
    void createValveTestScreen();
    void createMaintenanceScreen();
    void createFillPanel();
    void createCylinderScreen();
    void openCylinderPicker(bool bank);
    void refreshCylinderPicker();
    void applyMode();
    void updateFillReadouts();
    void loadFillSettings();
    void saveFillSettings();
    const CylinderSpec& fillCylinder() const;
    const CylinderSpec& bankCylinder() const;
    void updatePressureRates();
    void updateMaintenanceScreen();
    void updateValveTestScreen();
    void setValveTestOpening(float opening);
    void updatePressureCalScreen();
    void refreshPressureCalStatus();
    void showPressureCalResult(const char* action, PressureCalResult r, float volts);
    void refreshNetworkList();
    void loadBlenderSettings();
    void saveBlenderSettings();
    void applyBlenderSettings();
    void applyLayout();
    static lv_coord_t columnMargin(lv_coord_t total, int n, lv_coord_t* gap_out);
    lv_coord_t estopX() const;
    lv_coord_t estopWidth() const;
    void fillIpFields(const IpConfig& config);
    void setIpFieldsEnabled(bool enabled);
    void hideKeyboard();
    void createLeftPanel();
    void createStatusPanel();
    void updateStatusPanel();
    void createOxygenPanel();
    void createHeliumPanel();
    void createPressurePanel();
    void createPickers();

    void makePicker(lv_obj_t* col, KnobState* state, lv_color_t color);
    void createEmergencyStop();
    void updateEstopButton();
    void setTarget(KnobState* state, float value);
    void loadRemotePin();
    void saveRemotePin();
    void showCalibrationResult();
    void loadPresets();
    void savePresets();
    void resetPresets();
    void showPresetDialog();
    void closePresetDialog();

    static void picker_event_handler(lv_event_t* e);
    static void diagnostics_switch_handler(lv_event_t* e);
    static void estop_handler(lv_event_t* e);
    static void estop_resume_handler(lv_event_t* e);
    static void remote_switch_handler(lv_event_t* e);
    static void calibrate_btn_handler(lv_event_t* e);
    static void cal_confirm_handler(lv_event_t* e);
    static void msgbox_close_handler(lv_event_t* e);
    static void presets_btn_handler(lv_event_t* e);
    static void preset_select_handler(lv_event_t* e);
    static void preset_close_handler(lv_event_t* e);
    static void preset_mode_handler(lv_event_t* e);
    static void preset_save_handler(lv_event_t* e);
    static void preset_defaults_handler(lv_event_t* e);
    static void preset_delete_confirm_handler(lv_event_t* e);
    static void setup_open_handler(lv_event_t* e);
    static void setup_close_handler(lv_event_t* e);
    static void menu_back_handler(lv_event_t* e);
    static void menu_blender_handler(lv_event_t* e);
    static void menu_network_handler(lv_event_t* e);
    static void menu_pcal_handler(lv_event_t* e);
    static void menu_valvetest_handler(lv_event_t* e);
    static void menu_maint_handler(lv_event_t* e);
    static void mode_btn_handler(lv_event_t* e);
    static void mode_choice_handler(lv_event_t* e);
    static void bank_btn_handler(lv_event_t* e);
    static void cyl_btn_handler(lv_event_t* e);
    static void cyl_tile_handler(lv_event_t* e);
    static void cyl_step_handler(lv_event_t* e);
    static void maint_step_handler(lv_event_t* e);
    static void maint_reset_handler(lv_event_t* e);
    static void maint_reset_confirm_handler(lv_event_t* e);
    static void valvetest_back_handler(lv_event_t* e);
    static void valvetest_gas_handler(lv_event_t* e);
    static void valvetest_slider_handler(lv_event_t* e);
    static void valvetest_step_handler(lv_event_t* e);
    static void valvetest_close_handler(lv_event_t* e);
    static void valvetest_save_handler(lv_event_t* e);
    static void valvetest_forget_handler(lv_event_t* e);
    static void learning_handler(lv_event_t* e);
    static void pcal_zero_handler(lv_event_t* e);
    static void pcal_span_handler(lv_event_t* e);
    static void pcal_reset_handler(lv_event_t* e);
    static void units_handler(lv_event_t* e);
    static void gas_mode_handler(lv_event_t* e);
    static void transducers_handler(lv_event_t* e);
    static void ip_mode_handler(lv_event_t* e);
    static void ip_apply_handler(lv_event_t* e);
    static void textarea_handler(lv_event_t* e);
    static void keyboard_handler(lv_event_t* e);
    static void setup_scan_handler(lv_event_t* e);
    static void setup_connect_handler(lv_event_t* e);
};

#endif
