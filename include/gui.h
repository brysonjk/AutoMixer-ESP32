#ifndef GUI_H
#define GUI_H

#include <lvgl.h>
#include "sensors.h"
#include "valve_control.h"
#include "wifi_manager.h"


// A gas preset, O2/He in percent.
struct GasPreset {
    uint8_t o2;
    uint8_t he;
};

// Enough for four full rows in the presets grid without scrolling.
#define MAX_PRESETS 16

#define PSI_PER_BAR 14.5038f

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
    void setIpAddress(const char* ip);

    bool emergencyStopped() { return estop_active; }
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

private:
    OxygenSensor* sensors;
    ValveController* valve_ctrl;
    WifiManager* wifi;

    lv_obj_t* screen;
    lv_obj_t* screen_menu;      // Setup: the list of settings pages
    lv_obj_t* screen_blender;
    lv_obj_t* screen_network;
    lv_obj_t* screen_pcal;      // pressure calibration

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
    lv_obj_t* label_fill_psi;
    lv_obj_t* label_bank_unit;
    lv_obj_t* label_fill_unit;

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
