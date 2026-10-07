#include "gui.h"
#include "display_driver.h"
#include <math.h>
#include <Preferences.h>
#include <esp_random.h>

#define COLOR_BG          lv_color_hex(0x000000)
#define COLOR_O2          lv_color_hex(0x2BE82B)
#define COLOR_HE          lv_color_hex(0x2E7BFF)
#define COLOR_BTN         lv_color_hex(0x7A85C6)
#define COLOR_BTN_ACTIVE  lv_color_hex(0x9AA4DC)
#define COLOR_BTN_TEXT    lv_color_hex(0xF2F4FF)
#define COLOR_BTN_TEXT_AC lv_color_hex(0x1B2A8C)
#define COLOR_LABEL       lv_color_hex(0xE8ECF2)
#define COLOR_STATUS_BG   lv_color_hex(0x4A2740)
#define COLOR_STATUS_TEXT lv_color_hex(0xFF5566)
#define COLOR_BRAND       lv_color_hex(0x3D63FF)
// Pure black, not near-black: in RGB565 green keeps 6 bits to red/blue's 5, so a value
// like 0x050505 quantizes to (0,1,0) and shows a faint green cast.
#define COLOR_BOX         lv_color_hex(0x000000)

#define COLOR_BANNER      lv_color_hex(0x121829)
#define COLOR_REMOTE      lv_color_hex(0xFFB020)
#define BANNER_H          46

// Horizontal layout: up to four columns (left controls, oxygen, helium, pressure),
// centred with equal gaps and side margins by applyLayout(). The pressure column is
// narrower: four digits and no picker. Gaps stretch when helium is hidden, up to a limit
// so the remaining columns don't drift apart.
#define LEFT_W          147
#define COLUMN_W        170
#define COLUMN_P_W      140
#define COLUMN_GAP      28
#define MAX_COLUMN_GAP  80

// The target row: each gas column's picker wheel, the Target heading and the Fill
// readout are centred on it.
#define TARGET_ROW_Y 218
#define TARGET_ROW_H 78
#define BUTTON_H     42

// Bottom row: Calibrate at the left margin, Presets at the right, E-STOP filling between.
#define ESTOP_H             48
#define ESTOP_Y             (SCREEN_HEIGHT - ESTOP_H - 8)
#define BOTTOM_BTN_W        140
#define BOTTOM_GAP          10

// Status panel: across the full bottom-row width, between the target row and the E-STOP.
#define STATUS_Y            334
#define STATUS_H            72
#define COLOR_ESTOP         lv_color_hex(0xD32F2F)
#define COLOR_ESTOP_LATCHED lv_color_hex(0x7A1A1A)

#define PICKER_STEP      1.0f
#define PICKER_ROWS      101   // 0 to 100 inclusive
#define PICKER_W         130
#define COLOR_PICKER_BG  lv_color_hex(0x0E1426)
#define COLOR_PICKER_DIM lv_color_hex(0x7C869C)

static lv_obj_t* make_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                            lv_color_t color, lv_coord_t x, lv_coord_t y) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

// LVGL's built-in Montserrat faces are regular weight only, so weight is faked by
// drawing the text twice a pixel apart.
static void make_bold_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                            lv_color_t color, lv_coord_t x, lv_coord_t y) {
    make_label(parent, text, font, color, x, y);
    make_label(parent, text, font, color, x + 1, y);
}

// Only rewrites a label whose text actually changed. Setting text frees and reallocates
// the label's buffer and forces a redraw, which at 10 Hz for every readout is needless
// heap churn and drawing.
static void set_text_if_changed(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static lv_obj_t* make_box(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, lv_coord_t h, lv_color_t bg) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, bg, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 2, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static lv_obj_t* make_msgbox(const char* title, const char* text, const char* btns[]);
static void show_notice(const char* title, const char* text);

static lv_obj_t* make_button(lv_obj_t* parent, const char* text, lv_coord_t x, lv_coord_t y,
                             bool active) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LEFT_W, BUTTON_H);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, active ? COLOR_BTN_ACTIVE : COLOR_BTN, 0);
    lv_obj_set_style_radius(btn, 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, active ? COLOR_BTN_TEXT_AC : COLOR_BTN_TEXT, 0);
    lv_obj_center(label);
    return btn;
}


FillStationGUI::FillStationGUI(OxygenSensor* sensor, ValveController* valves,
                               WifiManager* wifi_mgr)
    : sensors(sensor), valve_ctrl(valves), wifi(wifi_mgr), screen_menu(NULL),
      screen_blender(NULL), screen_network(NULL),
      dropdown_networks(NULL), textarea_password(NULL), label_wifi_status(NULL),
      scan_pending(false), ip_mode(NULL), keyboard(NULL), keyboard_target(NULL),
      units_bar(false), helium_enabled(true), transducers_fitted(true),
      col_left(NULL), col_o2(NULL), col_he(NULL), col_p(NULL), bottom_margin_x(0),
      label_remote_badge(NULL), label_web_url(NULL),
      remote_enabled(false), cal_dialog(NULL), preset_overlay(NULL),
      preset_count(0), preset_edit_mode(false), preset_pending_delete(0),
      preset_view_count(0), diagnostics(false),
      estop_active(false), compressor_running(false), estop_dialog_open(false),
      estop_btn(NULL), estop_label(NULL), status_tone(-1), btn_calibrate(NULL), btn_presets(NULL) {
    o2_knob = {"O2", ADC_CH_OXYGEN, 21.0f, NULL};
    he_knob = {"He", ADC_CH_HELIUM, 0.0f, NULL};
    remote_pin[0] = '\0';
    textarea_pin = NULL;
    screen_pcal = NULL;
    keyboard_pcal = NULL;
    pcal_pending = 0;
    for (int i = 0; i < 2; i++) {
        pcal_now[i] = pcal_status[i] = pcal_ref[i] = pcal_ref_unit[i] = NULL;
    }
}

// The PIN persists so a remote user can keep using it; only the arming switch resets.
void FillStationGUI::loadRemotePin() {
    Preferences prefs;
    prefs.begin("web", false);
    if (prefs.isKey("pin")) {
        String stored = prefs.getString("pin", "");
        strlcpy(remote_pin, stored.c_str(), sizeof(remote_pin));
    }
    if (strlen(remote_pin) != 6) {
        snprintf(remote_pin, sizeof(remote_pin), "%06lu",
                 (unsigned long)(esp_random() % 1000000UL));
        prefs.putString("pin", remote_pin);
    }
    prefs.end();
}

// Takes the PIN typed on the Network page. Six digits, and not one of the guessable
// ones; otherwise the old PIN stays and the field is put back.
void FillStationGUI::saveRemotePin() {
    const char* entered = lv_textarea_get_text(textarea_pin);
    bool digits = strlen(entered) == 6;
    for (const char* c = entered; digits && *c; c++) digits = isdigit((unsigned char)*c);

    bool guessable = false;
    if (digits) {
        bool same = true, up = true, down = true;
        for (int i = 1; i < 6; i++) {
            same &= entered[i] == entered[0];
            up &= entered[i] == entered[i - 1] + 1;
            down &= entered[i] == entered[i - 1] - 1;
        }
        guessable = same || up || down;
    }

    if (!digits || guessable) {
        lv_textarea_set_text(textarea_pin, remote_pin);
        show_notice("PIN not changed",
                    !digits ? "The PIN must be exactly 6 digits."
                            : "Pick a PIN that isn't all one digit or a run like 123456.");
        return;
    }
    if (strcmp(entered, remote_pin) == 0) return;

    strlcpy(remote_pin, entered, sizeof(remote_pin));
    Preferences prefs;
    prefs.begin("web", false);
    prefs.putString("pin", remote_pin);
    prefs.end();
    Serial.println("remote PIN changed at the unit");
    show_notice("PIN changed", "Remote changes from the web dashboard now need the new PIN.");
}

void FillStationGUI::remote_switch_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->remote_enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (self->remote_enabled) {
        lv_obj_clear_flag(self->label_remote_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(self->label_remote_badge, LV_OBJ_FLAG_HIDDEN);
    }
    Serial.printf("remote control %s at the unit\n", self->remote_enabled ? "ARMED" : "off");
}

void FillStationGUI::setTarget(KnobState* state, float value) {
    if (value < 0.0f) value = 0.0f;
    if (value > 100.0f) value = 100.0f;
    const uint16_t row = (uint16_t)lroundf(value / PICKER_STEP);
    state->value = row * PICKER_STEP;
    // Setting the selection from code raises no VALUE_CHANGED, so value is set above.
    lv_roller_set_selected(state->picker, row, LV_ANIM_ON);
}

void FillStationGUI::setOxygenTarget(float value) { setTarget(&o2_knob, value); }
void FillStationGUI::setHeliumTarget(float value) {
    setTarget(&he_knob, helium_enabled ? value : 0.0f);
}

const char* FillStationGUI::compressorText() {
    return compressor_running ? "running" : "stopped";
}

const char* FillStationGUI::systemStatusText() {
    return lv_label_get_text(label_system_status);
}

void FillStationGUI::init() {
    loadRemotePin();
    loadPresets();
    loadBlenderSettings();

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_scr_load(screen);

    createBanner();
    createLeftPanel();
    createStatusPanel();
    createOxygenPanel();
    createHeliumPanel();
    createPressurePanel();
    createPickers();
    createSetupMenu();
    createBlenderScreen();
    createNetworkScreen();
    createPressureCalScreen();
    createValveTestScreen();

    createEmergencyStop();
    applyBlenderSettings();
}

void FillStationGUI::diagnostics_switch_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->diagnostics = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

// ---------------------------------------------------------------------------------
// Setup: a menu of settings pages, each its own screen with a Back button to the menu.

static lv_obj_t* make_action_button(lv_obj_t* parent, const char* text, lv_coord_t x,
                                    lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t bg) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return btn;
}

// A row of mutually exclusive choices, one always selected.
static lv_obj_t* make_segmented(lv_obj_t* parent, const char* map[], uint16_t selected,
                                lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* bm = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(bm, map);
    lv_btnmatrix_set_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(bm, true);
    lv_btnmatrix_set_btn_ctrl(bm, selected, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_size(bm, w, h);
    lv_obj_set_pos(bm, x, y);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_pad_all(bm, 0, 0);
    lv_obj_set_style_pad_column(bm, 8, 0);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_radius(bm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bm, COLOR_PICKER_BG, LV_PART_ITEMS);
    lv_obj_set_style_border_width(bm, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(bm, COLOR_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, COLOR_LABEL, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bm, COLOR_BTN_ACTIVE, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(bm, COLOR_BTN_TEXT_AC, LV_PART_ITEMS | LV_STATE_CHECKED);
    return bm;
}

lv_obj_t* FillStationGUI::makeSubScreen(const char* title, lv_event_cb_t back_handler) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* banner = make_box(scr, 0, 0, SCREEN_WIDTH, BANNER_H, COLOR_BANNER);
    make_bold_label(banner, title, &lv_font_montserrat_28, COLOR_BTN_ACTIVE, 16, 8);

    lv_obj_t* back = lv_btn_create(banner);
    lv_obj_set_size(back, 110, 34);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(back, COLOR_BTN, 0);
    lv_obj_set_style_radius(back, 3, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_handler, LV_EVENT_CLICKED, this);
    lv_obj_t* label = lv_label_create(back);
    lv_label_set_text(label, "Back");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, COLOR_BTN_TEXT, 0);
    lv_obj_center(label);
    return scr;
}

void FillStationGUI::createSetupMenu() {
    screen_menu = makeSubScreen("Setup", menu_back_handler);

    struct Item { const char* title; const char* note; lv_event_cb_t handler; };
    const Item items[] = {
        {"Blender Setup", "Pressure units, gas mode, pressure sensors", menu_blender_handler},
        {"Network", "WiFi, IP address, remote control", menu_network_handler},
        {"Pressure Calibration", "Zero and span for the bank and fill sensors",
         menu_pcal_handler},
        {"Valve Test", "Find where each valve starts to flow",
         menu_valvetest_handler},
    };
    // Four rows, kept clear of the E-STOP along the bottom.
    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(screen_menu);
        lv_obj_set_size(btn, 460, 78);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 56 + i * 88);
        lv_obj_set_style_bg_color(btn, COLOR_PICKER_BG, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, COLOR_BTN, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, items[i].handler, LV_EVENT_CLICKED, this);

        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_t* t = make_label(btn, items[i].title, &lv_font_montserrat_24, COLOR_LABEL, 0, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 14, 10);
        lv_obj_t* n = make_label(btn, items[i].note, &lv_font_montserrat_16, COLOR_PICKER_DIM,
                                 0, 0);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 14, 46);
        lv_obj_t* arrow = make_label(btn, LV_SYMBOL_RIGHT, &lv_font_montserrat_24, COLOR_BTN, 0, 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static const char* UNITS_MAP[] = {"PSI", "BAR", ""};
static const char* GAS_MODE_MAP[] = {"Trimix", "Nitrox only", ""};
static const char* TRANSDUCER_MAP[] = {"Fitted", "None", ""};
static const char* LEARNING_MAP[] = {"Auto", "Locked", ""};

void FillStationGUI::learning_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->valve_ctrl->setLearningLocked(lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1);
}

void FillStationGUI::createBlenderScreen() {
    screen_blender = makeSubScreen("Blender Setup", setup_close_handler);

    struct Row { const char* title; const char* note; };
    const Row rows[] = {
        {"Pressure units", "Bank and fill readings"},
        {"Gas mode", "Nitrox only hides helium and ignores sensor S1"},
        {"Pressure sensors", "Bank and fill transducers"},
        {"Valve learning", "Auto keeps learning each valve's holding opening"},
        {"Diagnostics", "Raw sensor mV; valve drive while blending"},
    };
    // Five rows, kept clear of the E-STOP along the bottom.
    for (int i = 0; i < 5; i++) {
        const lv_coord_t y = 66 + i * 70;
        make_label(screen_blender, rows[i].title, &lv_font_montserrat_24, COLOR_LABEL, 48, y);
        make_label(screen_blender, rows[i].note, &lv_font_montserrat_14, COLOR_PICKER_DIM, 48,
                   y + 30);
    }

    lv_obj_t* bm = make_segmented(screen_blender, UNITS_MAP, units_bar ? 1 : 0, 440, 66, 300, 46);
    lv_obj_add_event_cb(bm, units_handler, LV_EVENT_VALUE_CHANGED, this);
    bm = make_segmented(screen_blender, GAS_MODE_MAP, helium_enabled ? 0 : 1, 440, 136, 300, 46);
    lv_obj_add_event_cb(bm, gas_mode_handler, LV_EVENT_VALUE_CHANGED, this);
    bm = make_segmented(screen_blender, TRANSDUCER_MAP, transducers_fitted ? 0 : 1, 440, 206,
                        300, 46);
    lv_obj_add_event_cb(bm, transducers_handler, LV_EVENT_VALUE_CHANGED, this);
    bm = make_segmented(screen_blender, LEARNING_MAP, valve_ctrl->learningLocked() ? 1 : 0, 440,
                        276, 300, 46);
    lv_obj_add_event_cb(bm, learning_handler, LV_EVENT_VALUE_CHANGED, this);

    // Diagnostics: each gas's ADC channel and raw mV in its sensor line, and both valves'
    // opening and coil voltage on the status panel while blending. Not persisted; it's a
    // bench aid.
    lv_obj_t* diag = lv_switch_create(screen_blender);
    lv_obj_set_size(diag, 70, 36);
    lv_obj_set_pos(diag, 440, 351);
    lv_obj_add_event_cb(diag, diagnostics_switch_handler, LV_EVENT_VALUE_CHANGED, this);
}

void FillStationGUI::loadBlenderSettings() {
    Preferences prefs;
    prefs.begin("blender", false);
    units_bar = prefs.isKey("bar") && prefs.getBool("bar", false);
    helium_enabled = !prefs.isKey("he") || prefs.getBool("he", true);
    transducers_fitted = !prefs.isKey("xdcr") || prefs.getBool("xdcr", true);
    prefs.end();
}

void FillStationGUI::saveBlenderSettings() {
    Preferences prefs;
    prefs.begin("blender", false);
    prefs.putBool("bar", units_bar);
    prefs.putBool("he", helium_enabled);
    prefs.putBool("xdcr", transducers_fitted);
    prefs.end();
}

// Brings the main screen, sensors and layout in line with the Blender Setup choices.
void FillStationGUI::applyBlenderSettings() {
    sensors->setHeliumCellUsed(helium_enabled);
    if (helium_enabled) {
        lv_obj_clear_flag(col_he, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Nitrox only: no helium may be asked for, whatever a preset or the web page says.
        setHeliumTarget(0);
        lv_obj_add_flag(col_he, LV_OBJ_FLAG_HIDDEN);
    }

    // Hide the whole column, not just its readouts, so applyLayout() re-centres the rest
    // instead of keeping an empty slot on the right.
    if (transducers_fitted) lv_obj_clear_flag(col_p, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(col_p, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label_bank_unit, units_bar ? "BAR" : "PSI");
    lv_label_set_text(label_fill_unit, units_bar ? "BAR" : "PSI");

    applyLayout();
    Serial.printf("blender: %s, %s, pressure sensors %s\n", helium_enabled ? "trimix" : "nitrox only",
                  units_bar ? "BAR" : "PSI", transducers_fitted ? "fitted" : "none");
}

// Centres the visible columns with equal gaps and side margins, and places the bottom
// row of buttons.
void FillStationGUI::applyLayout() {
    lv_obj_t* cols[] = {col_left, col_o2, col_he, col_p};
    const lv_coord_t widths[] = {LEFT_W, COLUMN_W, COLUMN_W, COLUMN_P_W};
    lv_coord_t total = 0;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        if (lv_obj_has_flag(cols[i], LV_OBJ_FLAG_HIDDEN)) continue;
        total += widths[i];
        n++;
    }
    lv_coord_t gap;
    lv_coord_t x = columnMargin(total, n, &gap);
    for (int i = 0; i < 4; i++) {
        if (lv_obj_has_flag(cols[i], LV_OBJ_FLAG_HIDDEN)) continue;
        lv_obj_set_x(cols[i], x);
        x += widths[i] + gap;
    }

    // The bottom row keeps the full trimix-with-sensors margins whatever columns are
    // hidden, so Calibrate, E-STOP and Presets never change size or move.
    bottom_margin_x = columnMargin(LEFT_W + 2 * COLUMN_W + COLUMN_P_W, 4, NULL);
    lv_obj_set_pos(btn_calibrate, bottom_margin_x, ESTOP_Y);
    lv_obj_set_pos(btn_presets, SCREEN_WIDTH - bottom_margin_x - BOTTOM_BTN_W, ESTOP_Y);
    lv_obj_set_pos(status_panel, bottom_margin_x, STATUS_Y);
    lv_obj_set_width(status_panel, SCREEN_WIDTH - 2 * bottom_margin_x);
    if (estop_btn) {
        lv_obj_set_pos(estop_btn, estopX(), ESTOP_Y);
        lv_obj_set_width(estop_btn, estopWidth());
    }
}

// Left edge of n columns totalling `total` px, centred with equal gaps (returned through
// gap_out) clamped to COLUMN_GAP..MAX_COLUMN_GAP.
lv_coord_t FillStationGUI::columnMargin(lv_coord_t total, int n, lv_coord_t* gap_out) {
    lv_coord_t gap = (SCREEN_WIDTH - total) / (n + 1);
    if (gap < COLUMN_GAP) gap = COLUMN_GAP;
    if (gap > MAX_COLUMN_GAP) gap = MAX_COLUMN_GAP;
    if (gap_out) *gap_out = gap;
    return (SCREEN_WIDTH - total - gap * (n - 1)) / 2;
}

// The E-STOP spans the gap between Calibrate and Presets.
lv_coord_t FillStationGUI::estopX() const { return bottom_margin_x + BOTTOM_BTN_W + BOTTOM_GAP; }

lv_coord_t FillStationGUI::estopWidth() const {
    return SCREEN_WIDTH - 2 * bottom_margin_x - 2 * BOTTOM_BTN_W - 2 * BOTTOM_GAP;
}

void FillStationGUI::units_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->units_bar = lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1;
    self->saveBlenderSettings();
    self->applyBlenderSettings();
}

void FillStationGUI::gas_mode_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->helium_enabled = lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 0;
    self->saveBlenderSettings();
    self->applyBlenderSettings();
}

void FillStationGUI::transducers_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->transducers_fitted = lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 0;
    self->saveBlenderSettings();
    self->applyBlenderSettings();
}

// LVGL's stock symbol page omits ^ ~ ` and |, which are legal in a WPA passphrase and
// unreachable from any other mode. This page covers all printable US-ASCII punctuation
// and keeps the digit row.
static const char* kb_map_special[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "`", "~", "!", "@", "#", "$", "%", "^", "&", "*", "|", "\n",
    "(", ")", "-", "_", "=", "+", "[", "]", "{", "}", "\\", "\n",
    "abc", ";", ":", "'", "\"", ",", ".", "<", ">", "/", "?", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t kb_ctrl_special[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2, 6,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char* IP_MODE_MAP[] = {"DHCP", "Static IP", ""};
static const char* IP_FIELD_NAMES[] = {"IP address", "Subnet mask", "Gateway", "DNS server"};

void FillStationGUI::createNetworkScreen() {
    screen_network = makeSubScreen("Network", setup_close_handler);

    make_label(screen_network, "WiFi network", &lv_font_montserrat_16, COLOR_LABEL, 24, 62);
    dropdown_networks = lv_dropdown_create(screen_network);
    lv_obj_set_size(dropdown_networks, 430, 40);
    lv_obj_set_pos(dropdown_networks, 24, 84);
    lv_dropdown_set_options(dropdown_networks, "(scanning...)");
    lv_obj_set_style_text_font(dropdown_networks, &lv_font_montserrat_18, 0);

    lv_obj_t* btn = make_action_button(screen_network, "Rescan", 470, 84, 120, 40, COLOR_BTN);
    lv_obj_add_event_cb(btn, setup_scan_handler, LV_EVENT_CLICKED, this);

    make_label(screen_network, "Password", &lv_font_montserrat_16, COLOR_LABEL, 24, 134);
    textarea_password = lv_textarea_create(screen_network);
    lv_obj_set_size(textarea_password, 430, 44);
    lv_obj_set_pos(textarea_password, 24, 156);
    lv_textarea_set_one_line(textarea_password, true);
    lv_textarea_set_password_mode(textarea_password, true);
    lv_textarea_set_placeholder_text(textarea_password, "network password");
    lv_obj_set_style_text_font(textarea_password, &lv_font_montserrat_18, 0);
    lv_obj_add_event_cb(textarea_password, textarea_handler, LV_EVENT_ALL, this);

    btn = make_action_button(screen_network, "Connect", 470, 156, 120, 44, COLOR_BRAND);
    lv_obj_add_event_cb(btn, setup_connect_handler, LV_EVENT_CLICKED, this);

    label_wifi_status = make_label(screen_network, "not connected", &lv_font_montserrat_16,
                                   COLOR_LABEL, 24, 212);

    // Address settings. The fields always show an address: the saved static one, or
    // with DHCP the lease currently in use (greyed out), which makes a sensible starting
    // point when switching to a static address.
    const IpConfig ip = wifi->ipConfig();
    ip_mode = make_segmented(screen_network, IP_MODE_MAP, ip.static_ip ? 1 : 0, 24, 240, 230, 44);
    lv_obj_add_event_cb(ip_mode, ip_mode_handler, LV_EVENT_VALUE_CHANGED, this);
    btn = make_action_button(screen_network, "Apply", 270, 240, 120, 44, COLOR_BRAND);
    lv_obj_add_event_cb(btn, ip_apply_handler, LV_EVENT_CLICKED, this);

    for (int i = 0; i < 4; i++) {
        const lv_coord_t x = 24 + (i % 2) * 246;
        const lv_coord_t y = 310 + (i / 2) * 64;
        make_label(screen_network, IP_FIELD_NAMES[i], &lv_font_montserrat_14, COLOR_PICKER_DIM,
                   x, y - 18);
        lv_obj_t* ta = lv_textarea_create(screen_network);
        lv_obj_set_size(ta, 230, 40);
        lv_obj_set_pos(ta, x, y);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_accepted_chars(ta, "0123456789.");
        lv_textarea_set_max_length(ta, 15);
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(ta, COLOR_PICKER_DIM, LV_STATE_DISABLED);
        lv_obj_add_event_cb(ta, textarea_handler, LV_EVENT_ALL, this);
        ip_fields[i] = ta;
    }
    fillIpFields(ip.static_ip ? ip : wifi->currentAddress());
    setIpFieldsEnabled(ip.static_ip);

    // Remote control via the web dashboard. The switch is deliberately not persisted:
    // it starts off on every boot, so the unit can never come back up remotely armed.
    make_label(screen_network, "Remote control", &lv_font_montserrat_16, COLOR_LABEL, 610, 62);
    lv_obj_t* sw = lv_switch_create(screen_network);
    lv_obj_set_size(sw, 70, 36);
    lv_obj_set_pos(sw, 610, 86);
    lv_obj_set_style_bg_color(sw, COLOR_REMOTE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, remote_switch_handler, LV_EVENT_VALUE_CHANGED, this);

    // Tap to change; the number pad's OK saves it.
    make_label(screen_network, "PIN", &lv_font_montserrat_20, COLOR_LABEL, 610, 134);
    textarea_pin = lv_textarea_create(screen_network);
    lv_obj_set_size(textarea_pin, 120, 38);
    lv_obj_set_pos(textarea_pin, 656, 126);
    lv_textarea_set_one_line(textarea_pin, true);
    lv_textarea_set_accepted_chars(textarea_pin, "0123456789");
    lv_textarea_set_max_length(textarea_pin, 6);
    lv_textarea_set_text(textarea_pin, remote_pin);
    lv_obj_set_style_text_font(textarea_pin, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(textarea_pin, textarea_handler, LV_EVENT_ALL, this);
    label_web_url = make_label(screen_network, "", &lv_font_montserrat_14, COLOR_LABEL, 610, 174);

    // One keyboard, raised when a field is tapped: full width for the password, a number
    // pad to the right of the address fields for those, so the field stays visible.
    keyboard = lv_keyboard_create(screen_network);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, kb_map_special, kb_ctrl_special);
    lv_obj_add_event_cb(keyboard, keyboard_handler, LV_EVENT_ALL, this);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------------
// Pressure calibration: zero and span for each transducer.

static const char* PRESSURE_NAMES[2] = {"Bank", "Fill"};

void FillStationGUI::createPressureCalScreen() {
    screen_pcal = makeSubScreen("Pressure Calibration", setup_close_handler);

    for (int i = 0; i < 2; i++) {
        const lv_coord_t x = 24 + i * 392;
        make_label(screen_pcal, PRESSURE_NAMES[i], &lv_font_montserrat_28, COLOR_BTN_ACTIVE, x, 58);
        pcal_now[i] = make_label(screen_pcal, "--", &lv_font_montserrat_20, COLOR_LABEL, x, 98);

        lv_obj_t* btn = make_action_button(screen_pcal, "Set zero", x, 134, 170, 44, COLOR_BTN);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, pcal_zero_handler, LV_EVENT_CLICKED, this);
        btn = make_action_button(screen_pcal, "Reset", x + 190, 134, 170, 44, COLOR_STATUS_BG);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, pcal_reset_handler, LV_EVENT_CLICKED, this);

        make_label(screen_pcal, "Reference gauge reading", &lv_font_montserrat_14,
                   COLOR_PICKER_DIM, x, 192);
        lv_obj_t* ta = lv_textarea_create(screen_pcal);
        lv_obj_set_size(ta, 120, 42);
        lv_obj_set_pos(ta, x, 212);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_accepted_chars(ta, "0123456789.");
        lv_textarea_set_max_length(ta, 7);
        lv_textarea_set_placeholder_text(ta, "e.g. 3000");
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_18, 0);
        lv_obj_add_event_cb(ta, textarea_handler, LV_EVENT_ALL, this);
        pcal_ref[i] = ta;
        pcal_ref_unit[i] = make_label(screen_pcal, "PSI", &lv_font_montserrat_16, COLOR_LABEL,
                                      x + 128, 224);

        btn = make_action_button(screen_pcal, "Set span", x + 190, 212, 170, 42, COLOR_BRAND);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, pcal_span_handler, LV_EVENT_CLICKED, this);

        pcal_status[i] = make_label(screen_pcal, "", &lv_font_montserrat_14, COLOR_LABEL, x, 266);
    }

    lv_obj_t* help = make_label(screen_pcal,
        "Zero: vent the sensor to air, then tap Set zero.\n"
        "Span: pressurise it (ideally near a normal fill), type what a\n"
        "reference gauge reads, then tap Set span. Zero first.",
        &lv_font_montserrat_14, COLOR_PICKER_DIM, 24, 330);
    lv_obj_set_width(help, 480);

    keyboard_pcal = lv_keyboard_create(screen_pcal);
    lv_keyboard_set_mode(keyboard_pcal, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(keyboard_pcal, 270, 200);
    lv_obj_set_pos(keyboard_pcal, 520, 262);
    lv_obj_add_event_cb(keyboard_pcal, keyboard_handler, LV_EVENT_ALL, this);
    lv_obj_add_flag(keyboard_pcal, LV_OBJ_FLAG_HIDDEN);
}

// Live readings and units, while the page is showing.
void FillStationGUI::updatePressureCalScreen() {
    char buf[48];
    const char* unit = units_bar ? "BAR" : "PSI";
    const float scale = units_bar ? 1.0f / PSI_PER_BAR : 1.0f;
    for (int i = 0; i < 2; i++) {
        const float v = sensors->getPressureVolts((PressureChannel)i);
        const float p = sensors->getPressurePSI((PressureChannel)i) * scale;
        if (isnan(v)) snprintf(buf, sizeof(buf), "no signal");
        else snprintf(buf, sizeof(buf), "%.0f %s   (%.3f V)", p, unit, v);
        set_text_if_changed(pcal_now[i], buf);
        set_text_if_changed(pcal_ref_unit[i], unit);
    }
}

void FillStationGUI::refreshPressureCalStatus() {
    char buf[96];
    const float scale = units_bar ? 1.0f / PSI_PER_BAR : 1.0f;
    for (int i = 0; i < 2; i++) {
        const PressureCal c = sensors->getPressureCal((PressureChannel)i);
        snprintf(buf, sizeof(buf), "Zero: %.3f V  %s\nSpan: %.1f %s/V  %s", c.zero_v,
                 c.zero_set ? "(calibrated)" : "(datasheet)", c.psi_per_v * scale,
                 units_bar ? "BAR" : "PSI", c.span_set ? "(calibrated)" : "(datasheet)");
        lv_label_set_text(pcal_status[i], buf);
    }
}

void FillStationGUI::showPressureCalResult(const char* action, PressureCalResult r, float volts) {
    char text[220];
    const char* name = PRESSURE_NAMES[pcal_pending];
    switch (r) {
        case PCAL_OK:
            snprintf(text, sizeof(text), "%s %s saved (sensor at %.3f V).", name, action, volts);
            break;
        case PCAL_NO_READING:
            snprintf(text, sizeof(text),
                     "No reading from the %s sensor. Check it's connected and powered.", name);
            break;
        case PCAL_UNSTABLE:
            snprintf(text, sizeof(text),
                     "The %s reading was still changing. Let the pressure settle and try again.",
                     name);
            break;
        case PCAL_NOT_ZERO:
            snprintf(text, sizeof(text),
                     "The %s sensor reads %.2f V, too far from the %.2f V expected at 0 PSI. "
                     "Make sure it's vented to air.",
                     name, volts, PRESSURE_V_ZERO);
            break;
        case PCAL_BAD_SPAN:
            snprintf(text, sizeof(text),
                     "That reference doesn't fit the %s sensor's reading (%.2f V). Use at least "
                     "~500 PSI (35 bar), entered in the units shown, and set zero first.",
                     name, volts);
            break;
    }
    refreshPressureCalStatus();
    show_notice(r == PCAL_OK ? "Calibrated" : "Not saved", text);
}

void FillStationGUI::menu_pcal_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->refreshPressureCalStatus();
    self->updatePressureCalScreen();
    lv_scr_load(self->screen_pcal);
}

static uint8_t pcal_channel_of(lv_event_t* e) {
    return (uint8_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
}

void FillStationGUI::pcal_zero_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->hideKeyboard();
    self->pcal_pending = pcal_channel_of(e);
    static const char* btns[] = {"Set zero", "Cancel", ""};
    char text[96];
    snprintf(text, sizeof(text), "Vent the %s sensor to air (0 PSI gauge), then tap Set zero.",
             PRESSURE_NAMES[self->pcal_pending]);
    lv_obj_t* m = make_msgbox("Set zero", text, btns);
    lv_obj_add_event_cb(m, [](lv_event_t* ev) {
        FillStationGUI* gui = (FillStationGUI*)lv_event_get_user_data(ev);
        lv_obj_t* box = lv_event_get_current_target(ev);
        const bool go = strcmp(lv_msgbox_get_active_btn_text(box), "Set zero") == 0;
        lv_msgbox_close_async(box);
        if (!go) return;
        float volts;
        const PressureCalResult r =
            gui->sensors->calibratePressureZero((PressureChannel)gui->pcal_pending, &volts);
        gui->showPressureCalResult("zero", r, volts);
    }, LV_EVENT_VALUE_CHANGED, self);
}

void FillStationGUI::pcal_span_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->hideKeyboard();
    self->pcal_pending = pcal_channel_of(e);
    const float entered = atof(lv_textarea_get_text(self->pcal_ref[self->pcal_pending]));
    if (!(entered > 0.0f)) {
        show_notice("Set span", "Type the reference gauge's reading first.");
        return;
    }
    const float psi = self->units_bar ? entered * PSI_PER_BAR : entered;
    float volts;
    const PressureCalResult r =
        self->sensors->calibratePressureSpan((PressureChannel)self->pcal_pending, psi, &volts);
    if (r == PCAL_OK) lv_textarea_set_text(self->pcal_ref[self->pcal_pending], "");
    self->showPressureCalResult("span", r, volts);
}

void FillStationGUI::pcal_reset_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->hideKeyboard();
    self->pcal_pending = pcal_channel_of(e);
    static const char* btns[] = {"Reset", "Cancel", ""};
    char text[96];
    snprintf(text, sizeof(text), "Put the %s sensor back to its datasheet scaling?",
             PRESSURE_NAMES[self->pcal_pending]);
    lv_obj_t* m = make_msgbox("Reset calibration", text, btns);
    lv_obj_add_event_cb(m, [](lv_event_t* ev) {
        FillStationGUI* gui = (FillStationGUI*)lv_event_get_user_data(ev);
        lv_obj_t* box = lv_event_get_current_target(ev);
        const bool go = strcmp(lv_msgbox_get_active_btn_text(box), "Reset") == 0;
        lv_msgbox_close_async(box);
        if (!go) return;
        gui->sensors->clearPressureCal((PressureChannel)gui->pcal_pending);
        gui->refreshPressureCalStatus();
    }, LV_EVENT_VALUE_CHANGED, self);
}

void FillStationGUI::fillIpFields(const IpConfig& config) {
    const IPAddress values[] = {config.ip, config.mask, config.gateway, config.dns};
    for (int i = 0; i < 4; i++) {
        lv_textarea_set_text(ip_fields[i],
                             (uint32_t)values[i] == 0 ? "" : values[i].toString().c_str());
    }
}

void FillStationGUI::setIpFieldsEnabled(bool enabled) {
    for (lv_obj_t* ta : ip_fields) {
        if (enabled) lv_obj_clear_state(ta, LV_STATE_DISABLED);
        else lv_obj_add_state(ta, LV_STATE_DISABLED);
    }
}

void FillStationGUI::hideKeyboard() {
    if (keyboard_target) lv_obj_clear_state(keyboard_target, LV_STATE_FOCUSED);
    keyboard_target = NULL;
    lv_obj_t* kbs[] = {keyboard, keyboard_pcal};
    for (lv_obj_t* kb : kbs) {
        if (!kb) continue;
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

void FillStationGUI::textarea_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_obj_t* ta = lv_event_get_target(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DEFOCUSED) {
        if (ta == self->keyboard_target) self->hideKeyboard();
        // Tapping away without OK abandons a PIN edit rather than leaving it half-typed.
        if (ta == self->textarea_pin) lv_textarea_set_text(ta, self->remote_pin);
        return;
    }
    // CLICKED too: after OK the field keeps focus, so tapping it again sends no FOCUSED.
    if (code != LV_EVENT_FOCUSED && code != LV_EVENT_CLICKED) return;

    if (lv_obj_get_screen(ta) == self->screen_pcal) {
        // Number pad below the reference fields, clear of the E-STOP.
        lv_keyboard_set_mode(self->keyboard_pcal, LV_KEYBOARD_MODE_NUMBER);
        self->keyboard_target = ta;
        lv_keyboard_set_textarea(self->keyboard_pcal, ta);
        lv_obj_clear_flag(self->keyboard_pcal, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (ta == self->textarea_password) {
        lv_keyboard_set_mode(self->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        // Stops just above the E-STOP, which sits over every screen.
        lv_obj_set_size(self->keyboard, SCREEN_WIDTH, ESTOP_Y - 4 - 244);
        lv_obj_set_pos(self->keyboard, 0, 244);
    } else {
        lv_keyboard_set_mode(self->keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_obj_set_size(self->keyboard, 270, 226);
        lv_obj_set_pos(self->keyboard, 520, 240);
    }
    self->keyboard_target = ta;
    lv_keyboard_set_textarea(self->keyboard, ta);
    lv_obj_clear_flag(self->keyboard, LV_OBJ_FLAG_HIDDEN);
}

void FillStationGUI::keyboard_handler(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) return;
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    const bool pin = self->keyboard_target == self->textarea_pin;
    self->hideKeyboard();
    if (!pin) return;
    if (code == LV_EVENT_READY) self->saveRemotePin();
    else lv_textarea_set_text(self->textarea_pin, self->remote_pin);
}

void FillStationGUI::ip_mode_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    const bool is_static = lv_btnmatrix_get_selected_btn(self->ip_mode) == 1;
    self->hideKeyboard();
    self->setIpFieldsEnabled(is_static);
    // Back to DHCP: show the live lease again. To static: keep whatever is showing.
    if (!is_static) self->fillIpFields(self->wifi->currentAddress());
}

// Host-order view of an address, for mask arithmetic.
static uint32_t ip_bits(const IPAddress& a) {
    return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | a[3];
}

void FillStationGUI::ip_apply_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->hideKeyboard();

    IpConfig config = self->wifi->ipConfig();
    config.static_ip = lv_btnmatrix_get_selected_btn(self->ip_mode) == 1;
    char text[160];

    if (config.static_ip) {
        IPAddress values[4];
        for (int i = 0; i < 4; i++) {
            const char* s = lv_textarea_get_text(self->ip_fields[i]);
            // DNS may be left blank: the gateway usually answers DNS too.
            if (i == 3 && s[0] == '\0') {
                values[3] = values[2];
                continue;
            }
            if (!values[i].fromString(s) || ip_bits(values[i]) == 0) {
                snprintf(text, sizeof(text), "%s \"%s\" isn't a valid address.",
                         IP_FIELD_NAMES[i], s);
                show_notice("Can't apply", text);
                return;
            }
        }
        const uint32_t ip = ip_bits(values[0]), mask = ip_bits(values[1]),
                       gw = ip_bits(values[2]);
        const uint32_t host_bits = ~mask;
        if ((host_bits & (host_bits + 1)) != 0 || host_bits == 0) {
            show_notice("Can't apply", "The subnet mask isn't valid (e.g. 255.255.255.0).");
            return;
        }
        if ((ip & host_bits) == 0 || (ip & host_bits) == host_bits) {
            show_notice("Can't apply",
                        "That IP address is the subnet's network or broadcast address. "
                        "Pick another.");
            return;
        }
        if ((ip & mask) != (gw & mask) || ip == gw) {
            show_notice("Can't apply",
                        "The gateway must be a different address on the same subnet as the "
                        "IP address.");
            return;
        }
        config.ip = values[0];
        config.mask = values[1];
        config.gateway = values[2];
        config.dns = values[3];
        lv_textarea_set_text(self->ip_fields[3], values[3].toString().c_str());
        snprintf(text, sizeof(text),
                 "Saved. Reconnecting as %s.\n\nThe web dashboard moves to http://%s",
                 config.ip.toString().c_str(), config.ip.toString().c_str());
    } else {
        snprintf(text, sizeof(text), "Saved. Reconnecting and asking the router for an address.");
    }

    self->wifi->setIpConfig(config);
    show_notice(config.static_ip ? "Static IP" : "DHCP", text);
}

void FillStationGUI::refreshNetworkList() {
    const int count = wifi->networkCount();
    if (count <= 0) {
        lv_dropdown_set_options(dropdown_networks, count == 0 ? "(no networks found)"
                                                              : "(scanning...)");
        return;
    }

    String options;
    for (int i = 0; i < count; i++) {
        if (i > 0) options += "\n";
        // The dropdown splits options on newlines, so a network name containing one would
        // shift every later entry and Connect would pick the wrong network.
        String ssid = wifi->ssidAt(i);
        ssid.replace('\n', ' ');
        ssid.replace('\r', ' ');
        options += ssid + "  (" + String(wifi->rssiAt(i)) + " dBm)";
    }
    lv_dropdown_set_options(dropdown_networks, options.c_str());
}

void FillStationGUI::setup_open_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_scr_load(self->screen_menu);
}

// Back from a settings page, to the Setup menu.
void FillStationGUI::setup_close_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->hideKeyboard();
    lv_scr_load(self->screen_menu);
}

void FillStationGUI::menu_back_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_scr_load(self->screen);
}

void FillStationGUI::menu_blender_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_scr_load(self->screen_blender);
}

void FillStationGUI::menu_network_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_scr_load(self->screen_network);
    // Refresh the greyed-out DHCP lease, which may have changed since boot.
    if (lv_btnmatrix_get_selected_btn(self->ip_mode) == 0) {
        self->fillIpFields(self->wifi->currentAddress());
    }
    self->wifi->startScan();
    self->scan_pending = true;
    lv_dropdown_set_options(self->dropdown_networks, "(scanning...)");
}

void FillStationGUI::setup_scan_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->wifi->startScan();
    self->scan_pending = true;
    lv_dropdown_set_options(self->dropdown_networks, "(scanning...)");
}

void FillStationGUI::setup_connect_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->hideKeyboard();

    const int index = lv_dropdown_get_selected(self->dropdown_networks);
    if (self->wifi->networkCount() <= 0) return;

    const String ssid = self->wifi->ssidAt(index);
    const String password = lv_textarea_get_text(self->textarea_password);
    self->wifi->connect(ssid, password);
}

void FillStationGUI::createBanner() {
    lv_obj_t* banner = make_box(screen, 0, 0, SCREEN_WIDTH, BANNER_H, COLOR_BANNER);

    lv_obj_t* brand = make_label(banner, "DarkWaterDiving.com", &lv_font_montserrat_16,
                                 COLOR_BRAND, 0, 0);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 16, 0);

    // Name and version centred as one unit: a content-sized group, with the version
    // placed from the title's measured width rather than a hard-coded offset.
    lv_obj_t* title = lv_obj_create(banner);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* name = make_label(title, "Auto Mixer", &lv_font_montserrat_28,
                                COLOR_BTN_ACTIVE, 0, 0);
    make_label(title, "Auto Mixer", &lv_font_montserrat_28, COLOR_BTN_ACTIVE, 1, 0);
    lv_obj_update_layout(name);
    make_label(title, "v0.9", &lv_font_montserrat_16, COLOR_LABEL,
               lv_obj_get_width(name) + 10, 11);
    lv_obj_center(title);

    // Shown whenever remote control is armed, so anyone at the station can see it.
    label_remote_badge = make_label(banner, "REMOTE", &lv_font_montserrat_16, COLOR_REMOTE,
                                    0, 0);
    lv_obj_align(label_remote_badge, LV_ALIGN_RIGHT_MID, -134, 0);
    lv_obj_add_flag(label_remote_badge, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* setup = lv_btn_create(banner);
    lv_obj_set_size(setup, 110, 34);
    lv_obj_align(setup, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(setup, COLOR_BTN, 0);
    lv_obj_set_style_radius(setup, 3, 0);
    lv_obj_set_style_shadow_width(setup, 0, 0);
    lv_obj_add_event_cb(setup, setup_open_handler, LV_EVENT_CLICKED, this);

    lv_obj_t* label = lv_label_create(setup);
    lv_label_set_text(label, "Setup");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, COLOR_BTN_TEXT, 0);
    lv_obj_center(label);

    // Under the DarkWaterDiving.com name, left-aligned with it.
    label_ip = make_label(screen, "0.0.0.0", &lv_font_montserrat_16, COLOR_LABEL, 16,
                          BANNER_H + 6);
}

// A full-height column container. Transparent and not clickable, so it adds nothing on
// screen and taps pass through it to whatever is underneath (the banner included).
static lv_obj_t* make_column(lv_obj_t* screen, lv_coord_t w) {
    lv_obj_t* col = lv_obj_create(screen);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, w, SCREEN_HEIGHT);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return col;
}

static lv_obj_t* make_centered_label(lv_obj_t* parent, const char* text,
                                     const lv_font_t* font, lv_color_t color,
                                     lv_align_t align, lv_coord_t y);

// A row heading in the left column: styled like a button, but only a label.
static void make_row_heading(lv_obj_t* col, const char* text, lv_coord_t y) {
    lv_obj_t* box = make_box(col, 0, y, LEFT_W, BUTTON_H, COLOR_BTN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    make_centered_label(box, text, &lv_font_montserrat_24, COLOR_BTN_TEXT, LV_ALIGN_CENTER, 0);
}

void FillStationGUI::createLeftPanel() {
    col_left = make_column(screen, LEFT_W);
    make_row_heading(col_left, "Sensors", 128);
    make_row_heading(col_left, "Target", TARGET_ROW_Y + (TARGET_ROW_H - BUTTON_H) / 2);
}

// The one place that says what the blender is doing and, when the valves are held shut,
// why. Placed by applyLayout(); colours and text set by updateStatusPanel().
void FillStationGUI::createStatusPanel() {
    status_panel = make_box(screen, 0, STATUS_Y, 100, STATUS_H, COLOR_BOX);
    lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(status_panel, 6, 0);
    lv_obj_set_style_border_width(status_panel, 2, 0);

    status_dot = lv_obj_create(status_panel);
    lv_obj_remove_style_all(status_dot);
    lv_obj_set_size(status_dot, 18, 18);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_align(status_dot, LV_ALIGN_LEFT_MID, 20, 0);

    label_system_status = make_label(status_panel, "", &lv_font_montserrat_28, COLOR_LABEL, 54, 4);
    label_status_detail = make_label(status_panel, "", &lv_font_montserrat_16, COLOR_LABEL, 56, 40);
}

enum StatusTone { TONE_RED, TONE_AMBER, TONE_GREY, TONE_GREEN };

void FillStationGUI::updateStatusPanel() {
    char headline[24];
    char detail[80];
    StatusTone tone;
    const bool he = helium_enabled;

    if (estop_active) {
        tone = TONE_RED;
        strlcpy(headline, "EMERGENCY STOP", sizeof(headline));
        strlcpy(detail, "Valves closed. Tap the red button to resume.", sizeof(detail));
    } else if (!sensors->isAvailable()) {
        tone = TONE_RED;
        strlcpy(headline, "Sensor fault", sizeof(headline));
        strlcpy(detail, "ADC not responding. Valves held closed.", sizeof(detail));
    } else if (sensors->calibrating()) {
        tone = TONE_AMBER;
        strlcpy(headline, "Calibrating", sizeof(headline));
        strlcpy(detail, "Reading the cells on air. Valves held closed.", sizeof(detail));
    } else if (!sensors->isCalibrated()) {
        tone = TONE_AMBER;
        strlcpy(headline, "Not calibrated", sizeof(headline));
        strlcpy(detail, "Valves stay closed until the cells are calibrated. Tap Calibrate.",
                sizeof(detail));
    } else if (isnan(sensors->getOxygenPercent()) ||
               (he && isnan(sensors->getHeCellO2Percent()))) {
        // Mirrors the control loop, which shuts both valves on a faulted cell.
        tone = TONE_RED;
        strlcpy(headline, "Cell fault", sizeof(headline));
        strlcpy(detail, "An O2 cell reads out of range. Valves held closed.", sizeof(detail));
    } else if (!compressor_running) {
        tone = TONE_GREY;
        strlcpy(headline, "Ready", sizeof(headline));
        strlcpy(detail, "Compressor stopped. Valves open once it starts.", sizeof(detail));
    } else if (o2_over_target) {
        tone = TONE_AMBER;
        strlcpy(headline, "O2 over target", sizeof(headline));
        strlcpy(detail, "O2 valve held shut until the mix falls back to its target.",
                sizeof(detail));
    } else if (o2_knob.value <= 21.0f && he_knob.value <= 0.0f) {
        tone = TONE_GREY;
        strlcpy(headline, "Passing air", sizeof(headline));
        strlcpy(detail, "Compressor running. Targets are air, so the valves stay closed.",
                sizeof(detail));
    } else {
        tone = TONE_GREEN;
        if (he && he_knob.value > 0.0f) snprintf(headline, sizeof(headline), "Blending %.0f/%.0f", o2_knob.value, he_knob.value);
        else snprintf(headline, sizeof(headline), "Blending EAN%.0f", o2_knob.value);
        if (diagnostics) {
            // Average coil voltages, assuming the regulated 12 V supply the duty cap is
            // set for: a valve near 100% is running out of flow, not out of tuning.
            const float o2v = valve_ctrl->getO2ValvePosition(), hev = valve_ctrl->getHeValvePosition();
            snprintf(detail, sizeof(detail), "O2 valve %.0f%% (%.1f V)    He valve %.0f%% (%.1f V)",
                     o2v, o2v / 100.0f * VALVE_MAX_DUTY * 12.0f, hev,
                     hev / 100.0f * VALVE_MAX_DUTY * 12.0f);
        } else {
            strlcpy(detail, "Compressor running.", sizeof(detail));
        }
    }

    set_text_if_changed(label_system_status, headline);
    set_text_if_changed(label_status_detail, detail);
    if ((int)tone == status_tone) return;
    status_tone = tone;

    static const uint32_t fg[] = {0xFF5566, 0xFFB020, 0xC8D0DC, 0x2BE82B};
    static const uint32_t bg[] = {0x3A1418, 0x33270A, 0x161C2A, 0x0E2E16};
    const lv_color_t c = lv_color_hex(fg[tone]);
    lv_obj_set_style_bg_color(status_panel, lv_color_hex(bg[tone]), 0);
    lv_obj_set_style_border_color(status_panel, c, 0);
    lv_obj_set_style_border_opa(status_panel, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(status_dot, c, 0);
    lv_obj_set_style_text_color(label_system_status, c, 0);
}

static lv_obj_t* make_centered_label(lv_obj_t* parent, const char* text,
                                     const lv_font_t* font, lv_color_t color,
                                     lv_align_t align, lv_coord_t y) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_align(label, align, 0, y);
    return label;
}

// One gas column's readouts, inside its column container. Both gas columns share this
// geometry so every row lines up across them; the boxes are black on black, so they read
// as bare text. Labels are centre-aligned, so they stay centred as "--.-" turns into a
// live reading.
static void make_gas_column(lv_obj_t* col, const char* title_text, const char* mv_text,
                            lv_color_t color, lv_obj_t** value, lv_obj_t** mv) {
    lv_obj_t* box = make_box(col, 0, 88, COLUMN_W, 88, COLOR_BOX);
    make_centered_label(box, title_text, &lv_font_montserrat_32, color, LV_ALIGN_TOP_MID, 0);
    *value = make_centered_label(box, "--.-", &lv_font_montserrat_48, color,
                                 LV_ALIGN_BOTTOM_MID, 2);

    *mv = make_label(col, mv_text, &lv_font_montserrat_16, COLOR_LABEL, 0, 0);
    lv_obj_align(*mv, LV_ALIGN_TOP_MID, 0, 180);
}

void FillStationGUI::createOxygenPanel() {
    col_o2 = make_column(screen, COLUMN_W);
    make_gas_column(col_o2, "Oxygen", "S2: -- mV", COLOR_O2, &label_o2_value, &label_o2_mv);
}

void FillStationGUI::createHeliumPanel() {
    col_he = make_column(screen, COLUMN_W);
    make_gas_column(col_he, "Helium", "S1: -- mV  --%", COLOR_HE, &label_he_value,
                    &label_he_mv);
}

// One pressure readout, laid out on the same rows as the gas columns: title on top, value
// below, the unit underneath the box. Returns the box; value and unit come back through
// the pointers.
static lv_obj_t* make_pressure_readout(lv_obj_t* col, const char* title, lv_coord_t y,
                                       lv_coord_t h, lv_obj_t** value, lv_obj_t** unit) {
    lv_obj_t* box = make_box(col, 0, y, COLUMN_P_W, h, COLOR_BOX);
    make_centered_label(box, title, &lv_font_montserrat_24, COLOR_BTN_ACTIVE, LV_ALIGN_TOP_MID, 0);
    *value = make_centered_label(box, "----", &lv_font_montserrat_48, COLOR_LABEL,
                                 LV_ALIGN_BOTTOM_MID, 2);

    *unit = make_label(col, "PSI", &lv_font_montserrat_16, COLOR_PICKER_DIM, 0, 0);
    lv_obj_align(*unit, LV_ALIGN_TOP_MID, 0, y + h + 4);
    return box;
}

void FillStationGUI::createPressurePanel() {
    col_p = make_column(screen, COLUMN_P_W);
    make_pressure_readout(col_p, "Bank", 88, 88, &label_bank_psi, &label_bank_unit);
    make_pressure_readout(col_p, "Fill", TARGET_ROW_Y, TARGET_ROW_H, &label_fill_psi,
                          &label_fill_unit);

    // Calibrate and Presets sit in the bottom row beside the E-STOP (placed by
    // applyLayout()), so they stay even with no pressure sensors fitted.
    btn_calibrate = make_button(screen, "Calibrate", 0, ESTOP_Y, false);
    lv_obj_set_size(btn_calibrate, BOTTOM_BTN_W, ESTOP_H);
    lv_obj_add_event_cb(btn_calibrate, calibrate_btn_handler, LV_EVENT_CLICKED, this);

    btn_presets = make_button(screen, "Presets", 0, ESTOP_Y, false);
    lv_obj_set_size(btn_presets, BOTTOM_BTN_W, ESTOP_H);
    lv_obj_add_event_cb(btn_presets, presets_btn_handler, LV_EVENT_CLICKED, this);
}

// ---------------------------------------------------------------------------------
// Valve test: drive one valve by hand to find where its gas starts to flow. main.cpp
// applies the opening only under the same guards as blending (compressor running,
// cells calibrated, no E-STOP), and the valve shuts when the page is left, after
// VALVE_TEST_TIMEOUT_MS untouched, or on E-STOP.

#define VALVE_TEST_TIMEOUT_MS 120000
#define COLOR_WARN lv_color_hex(0xFFB020)

static const char* VALVE_TEST_GAS_MAP[] = {"O2", "Helium", ""};

void FillStationGUI::createValveTestScreen() {
    screen_valvetest = makeSubScreen("Valve Test", valvetest_back_handler);

    make_label(screen_valvetest, "Valve", &lv_font_montserrat_20, COLOR_LABEL, 24, 62);
    vt_gas = make_segmented(screen_valvetest, VALVE_TEST_GAS_MAP, 0, 24, 92, 300, 50);
    lv_obj_add_event_cb(vt_gas, valvetest_gas_handler, LV_EVENT_VALUE_CHANGED, this);

    make_label(screen_valvetest, "Opening", &lv_font_montserrat_20, COLOR_LABEL, 24, 160);
    vt_value = make_label(screen_valvetest, "", &lv_font_montserrat_28, COLOR_LABEL, 140, 154);

    vt_slider = lv_slider_create(screen_valvetest);
    lv_obj_set_size(vt_slider, 540, 22);
    lv_obj_set_pos(vt_slider, 34, 206);
    lv_slider_set_range(vt_slider, 0, 100);
    lv_obj_set_style_bg_color(vt_slider, lv_color_hex(0x2A3350), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(vt_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(vt_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vt_slider, COLOR_BTN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vt_slider, COLOR_BTN_ACTIVE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(vt_slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(vt_slider, valvetest_slider_handler, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* btn = make_action_button(screen_valvetest, "- 1", 24, 252, 80, 50, COLOR_BTN);
    lv_obj_set_user_data(btn, (void*)(intptr_t)-1);
    lv_obj_add_event_cb(btn, valvetest_step_handler, LV_EVENT_CLICKED, this);
    btn = make_action_button(screen_valvetest, "+ 1", 116, 252, 80, 50, COLOR_BTN);
    lv_obj_set_user_data(btn, (void*)(intptr_t)1);
    lv_obj_add_event_cb(btn, valvetest_step_handler, LV_EVENT_CLICKED, this);
    btn = make_action_button(screen_valvetest, "Close valve", 214, 252, 170, 50, COLOR_STATUS_BG);
    lv_obj_add_event_cb(btn, valvetest_close_handler, LV_EVENT_CLICKED, this);
    btn = make_action_button(screen_valvetest, "Save as start point", 402, 252, 230, 50,
                             COLOR_BRAND);
    lv_obj_add_event_cb(btn, valvetest_save_handler, LV_EVENT_CLICKED, this);

    vt_readings = make_label(screen_valvetest, "", &lv_font_montserrat_24, COLOR_LABEL, 650, 62);
    vt_starts = make_label(screen_valvetest, "", &lv_font_montserrat_16, COLOR_PICKER_DIM, 650, 132);
    vt_learned = make_label(screen_valvetest, "", &lv_font_montserrat_16, COLOR_PICKER_DIM, 650, 182);
    btn = make_action_button(screen_valvetest, "Forget", 650, 252, 140, 50, COLOR_STATUS_BG);
    lv_obj_add_event_cb(btn, valvetest_forget_handler, LV_EVENT_CLICKED, this);

    vt_status = make_label(screen_valvetest, "", &lv_font_montserrat_20, COLOR_LABEL, 24, 318);
    lv_obj_t* help = make_label(screen_valvetest,
        "With the compressor running and gas connected, raise the opening a step at a time.\n"
        "When the reading first starts to move, tap Save. Saving at 0% clears the start point.\n"
        "The valve closes when you leave this page, after 2 minutes untouched, or on E-STOP.",
        &lv_font_montserrat_14, COLOR_PICKER_DIM, 24, 352);
    lv_obj_set_width(help, 750);
}

bool FillStationGUI::valveTestActive() {
    return screen_valvetest && lv_scr_act() == screen_valvetest;
}

void FillStationGUI::setValveTestOpening(float opening) {
    vt_opening = constrain(opening, 0.0f, 100.0f);
    vt_touched_ms = millis();
    if (lv_slider_get_value(vt_slider) != (int32_t)lroundf(vt_opening)) {
        lv_slider_set_value(vt_slider, (int32_t)lroundf(vt_opening), LV_ANIM_OFF);
    }
}

void FillStationGUI::updateValveTestScreen() {
    char buf[96];
    if (vt_opening > 0.0f && millis() - vt_touched_ms > VALVE_TEST_TIMEOUT_MS) {
        setValveTestOpening(0);
        Serial.println("valve test: timed out, valve closed");
    }

    // Average coil voltage, assuming the regulated 12 V supply the duty cap is set for.
    snprintf(buf, sizeof(buf), "%.0f%%   (about %.1f V)", vt_opening,
             vt_opening / 100.0f * VALVE_MAX_DUTY * 12.0f);
    set_text_if_changed(vt_value, buf);

    const float o2 = sensors->getOxygenPercent();
    const float he = sensors->getHeliumPercent();
    char o2s[12], hes[12];
    snprintf(o2s, sizeof(o2s), isnan(o2) ? "--.-" : "%.1f", o2);
    snprintf(hes, sizeof(hes), isnan(he) ? "--.-" : "%.1f", he);
    snprintf(buf, sizeof(buf), "O2  %s%%\nHe  %s%%", o2s, hes);
    set_text_if_changed(vt_readings, buf);

    snprintf(buf, sizeof(buf), "Start points\nO2 %.0f%%   He %.0f%%",
             valve_ctrl->o2StartPoint(), valve_ctrl->heStartPoint());
    set_text_if_changed(vt_starts, buf);

    // What the control loops have learned: the opening that held a target.
    char line[2][32];
    for (int i = 0; i < 2; i++) {
        float t, c;
        if (valve_ctrl->learnedAt(i == 1, &t, &c) && t > 0.0f) {
            snprintf(line[i], sizeof(line[i]), "%s %.0f%% held %.0f%%", i ? "He" : "O2", c, t);
        } else if (valve_ctrl->learnedGain(i == 1) > 0.0f) {
            // Learned before the target and opening were saved alongside it.
            snprintf(line[i], sizeof(line[i]), "%s learned", i ? "He" : "O2");
        } else {
            snprintf(line[i], sizeof(line[i]), "%s not learned", i ? "He" : "O2");
        }
    }
    snprintf(buf, sizeof(buf), "Learned%s\n%s\n%s", valve_ctrl->learningLocked() ? " (locked)" : "",
             line[0], line[1]);
    set_text_if_changed(vt_learned, buf);

    // Mirrors the guards main.cpp applies before it drives anything.
    lv_color_t color = COLOR_WARN;
    if (estop_active) {
        snprintf(buf, sizeof(buf), "EMERGENCY STOP: valves held shut");
        color = COLOR_STATUS_TEXT;
    } else if (!sensors->isAvailable()) {
        snprintf(buf, sizeof(buf), "Sensor fault: valves held shut");
        color = COLOR_STATUS_TEXT;
    } else if (sensors->calibrating() || !sensors->isCalibrated()) {
        snprintf(buf, sizeof(buf), "Calibrate the O2 cells first: valves held shut");
    } else if (!compressor_running) {
        snprintf(buf, sizeof(buf), "Start the compressor: valves only open while it runs");
    } else if (vt_opening <= 0.0f) {
        snprintf(buf, sizeof(buf), "Ready. Raise the opening slowly and watch the reading.");
        color = COLOR_LABEL;
    } else {
        snprintf(buf, sizeof(buf), "%s valve driven at %.0f%%", vt_helium ? "Helium" : "O2",
                 vt_opening);
        color = COLOR_O2;
    }
    set_text_if_changed(vt_status, buf);
    lv_obj_set_style_text_color(vt_status, color, 0);
}

void FillStationGUI::menu_valvetest_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->setValveTestOpening(0);
    self->updateValveTestScreen();
    lv_scr_load(self->screen_valvetest);
}

void FillStationGUI::valvetest_back_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->setValveTestOpening(0);
    lv_scr_load(self->screen_menu);
}

void FillStationGUI::valvetest_gas_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    const bool helium = lv_btnmatrix_get_selected_btn(lv_event_get_target(e)) == 1;
    if (helium != self->vt_helium) self->setValveTestOpening(0);   // never carry an opening across
    self->vt_helium = helium;
}

void FillStationGUI::valvetest_slider_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->setValveTestOpening(lv_slider_get_value(lv_event_get_target(e)));
}

void FillStationGUI::valvetest_step_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    const int step = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    self->setValveTestOpening(self->vt_opening + step);
}

void FillStationGUI::valvetest_close_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->setValveTestOpening(0);
}

void FillStationGUI::valvetest_forget_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->valve_ctrl->forgetLearned();
    show_notice("Forgotten", "Learned openings cleared. The next blend to each target\n"
                             "is slower and learns them again.");
}

void FillStationGUI::valvetest_save_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    const float v = constrain(self->vt_opening, 0.0f, VALVE_START_MAX);
    self->valve_ctrl->setStartPoint(self->vt_helium, v);
    char buf[96];
    if (v <= 0.0f) {
        snprintf(buf, sizeof(buf), "%s valve start point cleared.", self->vt_helium ? "Helium" : "O2");
    } else {
        snprintf(buf, sizeof(buf), "%s valve now starts at %.0f%%.%s", self->vt_helium ? "Helium" : "O2",
                 v, self->vt_opening > VALVE_START_MAX ? "\n(The most allowed is 90%.)" : "");
    }
    show_notice("Saved", buf);
}

// ---------------------------------------------------------------------------------
// Calibration dialogs

static lv_obj_t* make_msgbox(const char* title, const char* text, const char* btns[]) {
    // NULL parent makes it modal: LVGL puts a click-blocking backdrop behind it.
    lv_obj_t* m = lv_msgbox_create(NULL, title, text, btns, false);
    lv_obj_set_width(m, 480);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, 0);

    // LVGL gives each msgbox button a fixed ~86 px regardless of its label, so longer
    // labels ("Stay stopped") overflow into their neighbours. Span the full width instead
    // and share it out, with a row tall enough to hit reliably.
    lv_obj_t* row = lv_msgbox_get_btns(m);
    if (row) {
        lv_obj_set_size(row, lv_pct(100), 56);
        lv_obj_set_style_pad_column(row, 12, 0);
    }
    lv_obj_center(m);
    return m;
}

static const char* CAL_CONFIRM_BTNS[] = {"Start", "Cancel", ""};
static const char* CAL_RESULT_BTNS[] = {"OK", ""};

void FillStationGUI::calibrate_btn_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    if (self->sensors->calibrating()) return;
    lv_obj_t* m = make_msgbox(
        self->helium_enabled ? "Calibrate O2 sensors" : "Calibrate O2 sensor",
        self->helium_enabled
            ? "Put both sensors in ambient air with no gas flowing, and let them settle.\n\n"
              "Valves are held closed while it runs (10 s)."
            : "Put the O2 sensor in ambient air with no gas flowing, and let it settle.\n\n"
              "Valves are held closed while it runs (10 s).",
        CAL_CONFIRM_BTNS);
    lv_obj_add_event_cb(m, cal_confirm_handler, LV_EVENT_VALUE_CHANGED, self);
}

void FillStationGUI::cal_confirm_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_obj_t* m = lv_event_get_current_target(e);
    const bool start = strcmp(lv_msgbox_get_active_btn_text(m), "Start") == 0;
    lv_msgbox_close_async(m);
    if (!start) return;

    if (self->sensors->startCalibration()) {
        self->cal_dialog = make_msgbox("Calibrating", "Reading sensors...", NULL);
    } else {
        self->showCalibrationResult();
    }
}

void FillStationGUI::showCalibrationResult() {
    char text[256];
    const char* title = "Calibration failed";
    switch (sensors->lastCalibration()) {
        case CAL_OK:
            title = "Calibrated";
            if (helium_enabled) {
                snprintf(text, sizeof(text), "Saved. In air:\nS2 (O2):  %.2f mV\nS1 (He):  %.2f mV",
                         sensors->getO2CellAirMV(), sensors->getHeCellAirMV());
            } else {
                snprintf(text, sizeof(text), "Saved. In air:\nS2 (O2):  %.2f mV",
                         sensors->getO2CellAirMV());
            }
            break;
        case CAL_NO_ADC:
            snprintf(text, sizeof(text), "Couldn't read the ADS1115. Check its wiring.");
            break;
        case CAL_UNSTABLE:
            snprintf(text, sizeof(text),
                     "Readings were still changing, so nothing was saved.\n"
                     "Leave the sensors in air a little longer and try again.");
            break;
        case CAL_NOISY:
            if (helium_enabled) {
                snprintf(text, sizeof(text),
                         "Readings were bouncing around (S2 +/-%.2f mV, S1 +/-%.2f mV), so "
                         "nothing was saved.\nCheck the cell wiring and grounding, and keep "
                         "the cells out of any draft.",
                         sensors->calNoiseO2MV(), sensors->calNoiseHeMV());
            } else {
                snprintf(text, sizeof(text),
                         "Readings were bouncing around (S2 +/-%.2f mV), so nothing was saved."
                         "\nCheck the cell wiring and grounding, and keep the cell out of any "
                         "draft.",
                         sensors->calNoiseO2MV());
            }
            break;
        case CAL_OUT_OF_RANGE:
            if (!helium_enabled) {
                snprintf(text, sizeof(text),
                         "Reading doesn't look like air (S2 %.2f mV; expected %.0f-%.0f mV). "
                         "Nothing was saved.\nCheck the sensor is connected and not in a gas "
                         "stream.",
                         sensors->calMeasuredO2MV(), CAL_MIN_AIR_MV, CAL_MAX_AIR_MV);
                break;
            }
            snprintf(text, sizeof(text),
                     "Readings don't look like air (S2 %.2f mV, S1 %.2f mV; expected "
                     "%.0f-%.0f mV). Nothing was saved.\nCheck the sensors are connected "
                     "and not in a gas stream.",
                     sensors->calMeasuredO2MV(), sensors->calMeasuredHeMV(), CAL_MIN_AIR_MV,
                     CAL_MAX_AIR_MV);
            break;
        default:
            snprintf(text, sizeof(text), "Calibration did not run.");
            break;
    }
    lv_obj_t* m = make_msgbox(title, text, CAL_RESULT_BTNS);
    lv_obj_add_event_cb(m, msgbox_close_handler, LV_EVENT_VALUE_CHANGED, NULL);
}

void FillStationGUI::msgbox_close_handler(lv_event_t* e) {
    lv_msgbox_close_async(lv_event_get_current_target(e));
}

// ---------------------------------------------------------------------------------
// Gas presets: O2/He in percent. User-editable and stored in NVS; seeded with these.

static const GasPreset DEFAULT_PRESETS[] = {
    {32, 0},  {36, 0},  {30, 30},
    {25, 25}, {21, 35}, {18, 35},
    {18, 45}, {15, 55}, {12, 65},
    {10, 50}, {10, 70},
};
static const uint8_t PRESETS_PER_ROW = 4;

// Nitrox first, then trimix by increasing helium, so the grid reads in a sensible order.
static int compare_presets(const void* a, const void* b) {
    const GasPreset* x = (const GasPreset*)a;
    const GasPreset* y = (const GasPreset*)b;
    if (x->he != y->he) return (int)x->he - (int)y->he;
    return (int)y->o2 - (int)x->o2;
}

void FillStationGUI::savePresets() {
    qsort(presets, preset_count, sizeof(GasPreset), compare_presets);
    Preferences prefs;
    prefs.begin("presets", false);
    prefs.putUChar("n", preset_count);
    // NVS can't hold a zero-length blob, so an emptied list is recorded by the count alone.
    if (preset_count > 0) prefs.putBytes("list", presets, preset_count * sizeof(GasPreset));
    else prefs.remove("list");
    prefs.end();
}

void FillStationGUI::resetPresets() {
    preset_count = sizeof(DEFAULT_PRESETS) / sizeof(DEFAULT_PRESETS[0]);
    memcpy(presets, DEFAULT_PRESETS, sizeof(DEFAULT_PRESETS));
    savePresets();
}

void FillStationGUI::loadPresets() {
    Preferences prefs;
    prefs.begin("presets", false);
    const bool stored = prefs.isKey("n");
    uint8_t n = stored ? prefs.getUChar("n", 0) : 0;
    if (n > MAX_PRESETS) n = MAX_PRESETS;
    // An operator who deleted every preset gets an empty list back, not the defaults.
    const bool readable = n == 0 || (prefs.isKey("list") &&
                                     prefs.getBytes("list", presets, n * sizeof(GasPreset)) ==
                                         n * sizeof(GasPreset));
    prefs.end();
    // First boot, or an unreadable list: start from the defaults.
    if (!stored || !readable) {
        resetPresets();
        return;
    }
    preset_count = n;
}

// Tells the operator something happened, on top of the presets dialog.
static void show_notice(const char* title, const char* text) {
    static const char* ok[] = {"OK", ""};
    lv_obj_t* m = make_msgbox(title, text, ok);
    lv_obj_add_event_cb(m, [](lv_event_t* e) {
        lv_msgbox_close_async(lv_event_get_current_target(e));
    }, LV_EVENT_VALUE_CHANGED, NULL);
}

void FillStationGUI::presets_btn_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->preset_edit_mode = false;
    self->showPresetDialog();
}

// Builds the presets dialog from scratch. Called again after every change, which keeps
// the grid in step with the list without patching buttons in place.
void FillStationGUI::showPresetDialog() {
    if (preset_overlay) lv_obj_del_async(preset_overlay);

    // In Nitrox only mode the grid holds just the helium-free presets, labelled EANxx.
    preset_view_count = 0;
    for (int i = 0; i < preset_count; i++) {
        if (helium_enabled || presets[i].he == 0) preset_view[preset_view_count++] = i;
    }
    // The button map must outlive the grid, so it's static. Two alternate: the previous
    // grid is only deleted asynchronously and may still draw from its map once more, so
    // rebuilding into the same arrays would change its labels underneath it.
    static char label_sets[2][MAX_PRESETS][8];
    static const char* map_sets[2][MAX_PRESETS + MAX_PRESETS / PRESETS_PER_ROW + 1];
    static uint8_t set = 0;
    set ^= 1;
    char (*labels)[8] = label_sets[set];
    const char** map = map_sets[set];
    int m = 0;
    for (int v = 0; v < preset_view_count; v++) {
        const GasPreset& p = presets[preset_view[v]];
        if (v > 0 && v % PRESETS_PER_ROW == 0) map[m++] = "\n";
        if (helium_enabled) snprintf(labels[v], sizeof(labels[v]), "%u/%u", p.o2, p.he);
        else snprintf(labels[v], sizeof(labels[v]), "EAN%u", p.o2);
        map[m++] = labels[v];
    }
    map[m] = "";

    // Full-screen dimmed backdrop, so the screen underneath can't be touched meanwhile.
    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(overlay, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    preset_overlay = overlay;

    // Top-aligned and sized to finish above the E-STOP, which overlays every screen.
    lv_obj_t* panel = make_box(overlay, 0, 0, 600, ESTOP_Y - 8 - 36, COLOR_PICKER_BG);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t* title = make_label(panel,
                                 preset_edit_mode   ? "Edit presets"
                                 : helium_enabled ? "Gas presets  (O2 / He)"
                                                  : "Nitrox presets",
                                 &lv_font_montserrat_24, COLOR_LABEL, 0, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    char note_text[96];
    if (preset_edit_mode) {
        snprintf(note_text, sizeof(note_text),
                 "Tap a preset to delete it.  %u of %u slots used.", preset_count, MAX_PRESETS);
    } else if (helium_enabled) {
        snprintf(note_text, sizeof(note_text),
                 "Below 18%% O2 is hypoxic: not breathable at the surface.");
    } else {
        snprintf(note_text, sizeof(note_text), "Trimix presets are hidden in Nitrox only mode.");
    }
    lv_obj_t* note = make_label(panel, note_text, &lv_font_montserrat_14,
                                preset_edit_mode ? COLOR_STATUS_TEXT : COLOR_PICKER_DIM, 0, 0);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 48);

    if (preset_view_count == 0) {
        lv_obj_t* empty = make_label(panel, "No presets.\nTap Edit, then Save current.",
                                     &lv_font_montserrat_20, COLOR_PICKER_DIM, 0, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, -10);
    } else {
        const int rows = (preset_view_count + PRESETS_PER_ROW - 1) / PRESETS_PER_ROW;
        lv_obj_t* grid = lv_btnmatrix_create(panel);
        lv_btnmatrix_set_map(grid, map);
        lv_obj_set_size(grid, 560, rows * 58 + 8);
        lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 74);
        lv_obj_set_style_text_font(grid, &lv_font_montserrat_24, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        // Delete mode gets the warning colour, so it can't be mistaken for picking a mix.
        lv_obj_set_style_bg_color(grid, preset_edit_mode ? COLOR_STATUS_BG : COLOR_BTN,
                                  LV_PART_ITEMS);
        lv_obj_set_style_text_color(grid, COLOR_BTN_TEXT, LV_PART_ITEMS);
        lv_obj_add_event_cb(grid, preset_select_handler, LV_EVENT_VALUE_CHANGED, this);
    }

    // Bottom row of actions, spread evenly.
    lv_obj_t* actions = lv_obj_create(panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, 560, BUTTON_H);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    if (preset_edit_mode) {
        char save_text[24];
        if (helium_enabled) {
            snprintf(save_text, sizeof(save_text), "Save %.0f/%.0f", o2_knob.value, he_knob.value);
        } else {
            snprintf(save_text, sizeof(save_text), "Save EAN%.0f", o2_knob.value);
        }
        lv_obj_t* b = make_button(actions, save_text, 0, 0, false);
        lv_obj_set_width(b, 170);
        lv_obj_add_event_cb(b, preset_save_handler, LV_EVENT_CLICKED, this);

        b = make_button(actions, "Defaults", 0, 0, false);
        lv_obj_add_event_cb(b, preset_defaults_handler, LV_EVENT_CLICKED, this);

        b = make_button(actions, "Done", 0, 0, true);
        lv_obj_add_event_cb(b, preset_mode_handler, LV_EVENT_CLICKED, this);
    } else {
        lv_obj_t* b = make_button(actions, "Edit", 0, 0, false);
        lv_obj_add_event_cb(b, preset_mode_handler, LV_EVENT_CLICKED, this);

        b = make_button(actions, "Close", 0, 0, true);
        lv_obj_add_event_cb(b, preset_close_handler, LV_EVENT_CLICKED, this);
    }
}

void FillStationGUI::closePresetDialog() {
    if (preset_overlay) lv_obj_del_async(preset_overlay);
    preset_overlay = NULL;
}

void FillStationGUI::preset_close_handler(lv_event_t* e) {
    ((FillStationGUI*)lv_event_get_user_data(e))->closePresetDialog();
}

void FillStationGUI::preset_mode_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    self->preset_edit_mode = !self->preset_edit_mode;
    self->showPresetDialog();
}

void FillStationGUI::preset_select_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    const uint16_t view_id = lv_btnmatrix_get_selected_btn(lv_event_get_target(e));
    if (view_id >= self->preset_view_count) return;
    const uint8_t id = self->preset_view[view_id];
    const GasPreset p = self->presets[id];

    if (!self->preset_edit_mode) {
        self->setOxygenTarget(p.o2);
        self->setHeliumTarget(p.he);
        Serial.printf("preset %u/%u selected\n", p.o2, p.he);
        self->closePresetDialog();
        return;
    }

    // Deleting asks first: a stray tap in edit mode shouldn't silently lose a preset.
    self->preset_pending_delete = id;
    static const char* btns[] = {"Delete", "Cancel", ""};
    char text[48];
    snprintf(text, sizeof(text), "Delete preset %u/%u?", p.o2, p.he);
    lv_obj_t* m = make_msgbox("Delete preset", text, btns);
    lv_obj_add_event_cb(m, preset_delete_confirm_handler, LV_EVENT_VALUE_CHANGED, self);
}

void FillStationGUI::preset_delete_confirm_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_obj_t* m = lv_event_get_current_target(e);
    const bool confirmed = strcmp(lv_msgbox_get_active_btn_text(m), "Delete") == 0;
    lv_msgbox_close_async(m);

    const uint8_t id = self->preset_pending_delete;
    if (!confirmed || id >= self->preset_count) return;
    Serial.printf("preset %u/%u deleted\n", self->presets[id].o2, self->presets[id].he);
    memmove(&self->presets[id], &self->presets[id + 1],
            (self->preset_count - id - 1) * sizeof(GasPreset));
    self->preset_count--;
    self->savePresets();
    self->showPresetDialog();
}

void FillStationGUI::preset_save_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    const int o2 = (int)lroundf(self->o2_knob.value);
    const int he = self->helium_enabled ? (int)lroundf(self->he_knob.value) : 0;
    char text[96];

    if (o2 <= 0 || o2 + he > 100) {
        snprintf(text, sizeof(text),
                 "%d/%d isn't a possible mix: O2 must be above 0 and O2 + He at most 100.",
                 o2, he);
        show_notice("Can't save preset", text);
        return;
    }
    for (int i = 0; i < self->preset_count; i++) {
        if (self->presets[i].o2 == o2 && self->presets[i].he == he) {
            snprintf(text, sizeof(text), "%d/%d is already a preset.", o2, he);
            show_notice("Can't save preset", text);
            return;
        }
    }
    if (self->preset_count >= MAX_PRESETS) {
        snprintf(text, sizeof(text), "All %u slots are full. Delete one first.", MAX_PRESETS);
        show_notice("Can't save preset", text);
        return;
    }

    self->presets[self->preset_count++] = {(uint8_t)o2, (uint8_t)he};
    self->savePresets();
    Serial.printf("preset %d/%d saved\n", o2, he);
    self->showPresetDialog();
}

void FillStationGUI::preset_defaults_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    static const char* btns[] = {"Restore", "Cancel", ""};
    lv_obj_t* m = make_msgbox("Restore defaults",
                              "Replace your presets with the original list?", btns);
    lv_obj_add_event_cb(m, [](lv_event_t* ev) {
        FillStationGUI* gui = (FillStationGUI*)lv_event_get_user_data(ev);
        lv_obj_t* box = lv_event_get_current_target(ev);
        const bool confirmed = strcmp(lv_msgbox_get_active_btn_text(box), "Restore") == 0;
        lv_msgbox_close_async(box);
        if (!confirmed) return;
        gui->resetPresets();
        gui->showPresetDialog();
    }, LV_EVENT_VALUE_CHANGED, self);
}

// ---------------------------------------------------------------------------------
// Emergency stop

void FillStationGUI::createEmergencyStop() {
    // On the system layer, which LVGL draws and hit-tests above every screen and every
    // pop-up, so the stop stays reachable from Setup and from inside any dialog.
    estop_btn = lv_btn_create(lv_layer_sys());
    // Position and width follow the layout; see applyLayout().
    lv_obj_set_size(estop_btn, estopWidth(), ESTOP_H);
    lv_obj_set_pos(estop_btn, estopX(), ESTOP_Y);
    lv_obj_set_style_radius(estop_btn, 6, 0);
    lv_obj_set_style_shadow_width(estop_btn, 0, 0);
    lv_obj_set_style_border_width(estop_btn, 2, 0);
    lv_obj_set_style_border_color(estop_btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(estop_btn, LV_OPA_40, 0);
    // PRESSED, not CLICKED: CLICKED waits for the finger to lift, and a stop shouldn't.
    lv_obj_add_event_cb(estop_btn, estop_handler, LV_EVENT_PRESSED, this);

    estop_label = lv_label_create(estop_btn);
    lv_obj_set_style_text_color(estop_label, lv_color_white(), 0);
    lv_obj_center(estop_label);
    updateEstopButton();
}

void FillStationGUI::updateEstopButton() {
    lv_obj_set_style_bg_color(estop_btn, estop_active ? COLOR_ESTOP_LATCHED : COLOR_ESTOP, 0);
    lv_obj_set_style_text_font(estop_label,
                               estop_active ? &lv_font_montserrat_20 : &lv_font_montserrat_28, 0);
    lv_label_set_text(estop_label, estop_active ? "STOPPED  -  tap to resume" : "EMERGENCY STOP");
}

void FillStationGUI::triggerEmergencyStop() {
    // Close first, then everything else: don't wait for the control loop's next pass.
    valve_ctrl->closeAllValves();
    estop_active = true;
    // O2 20 rather than 21: the wheel is whole numbers, and at 21 the loop could still add
    // a trace of O2 to 20.9% air. Air then flows through unaltered once resumed.
    setOxygenTarget(20);
    setHeliumTarget(0);
    updateEstopButton();
    Serial.println("EMERGENCY STOP: valves closed, targets 20/0");
}

void FillStationGUI::estop_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    if (!self->estop_active) {
        self->triggerEmergencyStop();
        return;
    }
    if (self->estop_dialog_open) return;
    self->estop_dialog_open = true;
    // Resuming is deliberate: a confirmation, so a double tap can't undo the stop.
    static const char* btns[] = {"Resume", "Stay stopped", ""};
    char text[140];
    snprintf(text, sizeof(text),
             "Valves are closed.\n\nResume blending at %.0f/%.0f?", self->o2_knob.value,
             self->he_knob.value);
    lv_obj_t* m = make_msgbox("Emergency stop active", text, btns);
    lv_obj_add_event_cb(m, estop_resume_handler, LV_EVENT_VALUE_CHANGED, self);
}

void FillStationGUI::estop_resume_handler(lv_event_t* e) {
    FillStationGUI* self = (FillStationGUI*)lv_event_get_user_data(e);
    lv_obj_t* m = lv_event_get_current_target(e);
    const bool resume = strcmp(lv_msgbox_get_active_btn_text(m), "Resume") == 0;
    lv_msgbox_close_async(m);
    self->estop_dialog_open = false;
    if (!resume || !self->estop_active) return;
    self->estop_active = false;
    self->updateEstopButton();
    Serial.printf("emergency stop released at %.0f/%.0f\n", self->o2_knob.value,
                  self->he_knob.value);
}

// iOS-style picker wheel: drag up/down, it flings and snaps to a row. One row per
// PICKER_STEP from 0 to 100, so the selected row index maps straight to a percentage.
void FillStationGUI::makePicker(lv_obj_t* col, KnobState* state, lv_color_t color) {
    String options;
    for (int i = 0; i <= PICKER_ROWS - 1; i++) {
        if (i > 0) options += "\n";
        options += String((int)lroundf(i * PICKER_STEP));
    }

    lv_obj_t* roller = lv_roller_create(col);
    // Normal mode stops at 0 and 100; infinite mode would wrap 100 straight back to 0.
    lv_roller_set_options(roller, options.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_width(roller, PICKER_W);

    lv_obj_set_style_text_font(roller, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_line_space(roller, 6, 0);
    lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(roller, COLOR_PICKER_DIM, 0);
    lv_obj_set_style_bg_color(roller, COLOR_PICKER_BG, 0);
    lv_obj_set_style_radius(roller, 10, 0);
    lv_obj_set_style_border_width(roller, 1, 0);
    lv_obj_set_style_border_color(roller, color, 0);
    lv_obj_set_style_border_opa(roller, LV_OPA_50, 0);

    // The selected row: a band in the gas colour with white text.
    lv_obj_set_style_bg_color(roller, color, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_40, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_white(), LV_PART_SELECTED);

    lv_roller_set_selected(roller, (uint16_t)lroundf(state->value / PICKER_STEP), LV_ANIM_OFF);

    // Centred on the target row, level with the Target heading.
    lv_obj_update_layout(roller);
    const lv_coord_t h = lv_obj_get_height(roller);
    const lv_coord_t top = TARGET_ROW_Y + (TARGET_ROW_H - h) / 2;
    lv_obj_align(roller, LV_ALIGN_TOP_MID, 0, top);

    const lv_coord_t bottom = top + h;
    if (bottom > ESTOP_Y - 4) {
        Serial.printf("layout: picker bottom %d overlaps E-STOP at %d\n", bottom, ESTOP_Y);
    }

    state->picker = roller;
    lv_obj_add_event_cb(roller, picker_event_handler, LV_EVENT_VALUE_CHANGED, state);
}

void FillStationGUI::createPickers() {
    makePicker(col_o2, &o2_knob, COLOR_O2);
    makePicker(col_he, &he_knob, COLOR_HE);
}

void FillStationGUI::picker_event_handler(lv_event_t* e) {
    KnobState* state = (KnobState*)lv_event_get_user_data(e);
    state->value = lv_roller_get_selected(state->picker) * PICKER_STEP;
    Serial.printf("%s target -> %.0f%%\n", state->name, state->value);
}

void FillStationGUI::setCompressorRunning(bool running) { compressor_running = running; }


void FillStationGUI::setIpAddress(const char* ip) {
    lv_label_set_text(label_ip, ip);
}

// A cell's mV for display: "--" when the read failed, rather than "nan".
static const char* mv_text(float mv, char* out, size_t len) {
    if (isnan(mv)) snprintf(out, len, "--");
    else snprintf(out, len, "%.1f", mv);
    return out;
}

void FillStationGUI::update() {
    char buf[48];

    if (cal_dialog) {
        if (sensors->calibrating()) {
            snprintf(buf, sizeof(buf), "Reading sensors...  %u s",
                     sensors->calibrationSecondsLeft());
            lv_label_set_text(lv_msgbox_get_text(cal_dialog), buf);
        } else {
            lv_msgbox_close(cal_dialog);
            cal_dialog = NULL;
            showCalibrationResult();
        }
    }

    updateStatusPanel();

    if (lv_scr_act() == screen_pcal) updatePressureCalScreen();
    if (lv_scr_act() == screen_valvetest) updateValveTestScreen();

    if (scan_pending && !wifi->scanning()) {
        scan_pending = false;
        refreshNetworkList();
    }
    set_text_if_changed(label_wifi_status, wifi->statusText().c_str());
    const String ip = wifi->ip();
    set_text_if_changed(label_ip, ip.c_str());
    set_text_if_changed(label_web_url, wifi->connected() ? ("http://" + ip).c_str() : "no network");

    if (sensors->isAvailable()) {
        // NAN means the cell reads as disconnected or dead, so show dashes, not a number.
        const float o2 = sensors->getOxygenPercent();
        snprintf(buf, sizeof(buf), isnan(o2) ? "--.-" : "%.1f", o2);
        set_text_if_changed(label_o2_value, buf);
        char mv[12];
        snprintf(buf, sizeof(buf), "S2: %smV", mv_text(sensors->getOxygenMillivolts(), mv, sizeof(mv)));
        set_text_if_changed(label_o2_mv, buf);

        const float he = sensors->getHeliumPercent();
        snprintf(buf, sizeof(buf), isnan(he) ? "--.-" : "%.1f", he);
        set_text_if_changed(label_he_value, buf);
        // The helium cell is an O2 cell, so its line shows the raw O2 it sees, as on the
        // reference unit ("S1: 10.6mV 21.9%").
        const float he_cell_o2 = sensors->getHeCellO2Percent();
        if (isnan(he_cell_o2)) {
            snprintf(buf, sizeof(buf), "S1: %smV  cell fault",
                     mv_text(sensors->getHeliumMillivolts(), mv, sizeof(mv)));
        } else {
            snprintf(buf, sizeof(buf), "S1: %smV %.1f%%",
                     mv_text(sensors->getHeliumMillivolts(), mv, sizeof(mv)), he_cell_o2);
        }
        set_text_if_changed(label_he_mv, buf);
    }

    // NAN covers both "no ADC" and "transducer signal out of range", so a broken wire
    // shows as dashes rather than a plausible-looking 0.
    if (transducers_fitted) {
        const float scale = units_bar ? 1.0f / PSI_PER_BAR : 1.0f;
        const float bank = sensors->getBankPSI() * scale;
        const float fill = sensors->getFillPSI() * scale;
        snprintf(buf, sizeof(buf), isnan(bank) ? "----" : "%.0f", bank);
        set_text_if_changed(label_bank_psi, buf);
        snprintf(buf, sizeof(buf), isnan(fill) ? "----" : "%.0f", fill);
        set_text_if_changed(label_fill_psi, buf);
    }

    // Diagnostics replace each gas's sensor line with its ADC channel and raw mV. The
    // valve openings and coil voltages are on the status panel while blending.
    if (diagnostics) {
        char mv[12];
        snprintf(buf, sizeof(buf), "A%u %smV", o2_knob.channel,
                 mv_text(sensors->getOxygenMillivolts(), mv, sizeof(mv)));
        set_text_if_changed(label_o2_mv, buf);
        snprintf(buf, sizeof(buf), "A%u %smV", he_knob.channel,
                 mv_text(sensors->getHeliumMillivolts(), mv, sizeof(mv)));
        set_text_if_changed(label_he_mv, buf);
    }
}

