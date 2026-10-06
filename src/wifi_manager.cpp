#include "wifi_manager.h"
#include <WiFi.h>
#include <Preferences.h>

static const char* PREF_NAMESPACE = "wifi";
static const char* NET_NAMESPACE = "net";
static const uint32_t CONNECT_TIMEOUT_MS = 20000;

// Consecutive failed attempts before giving up. Each attempt bursts the radio at high
// current, which disturbs the display, so a bad password must not retry forever.
static const uint8_t MAX_CONNECT_FAILURES = 5;

static Preferences prefs;

// Written from the WiFi event task, read from the main loop.
static volatile uint8_t fail_count = 0;
static volatile uint8_t last_reason = 0;
static volatile bool give_up_requested = false;
static volatile bool handshake_failed = false;

// 15/202/204: the WPA handshake failed or timed out.
static bool is_handshake_failure(uint8_t reason) {
    return reason == 15 || reason == 202 || reason == 204;
}

WifiManager::WifiManager() : scan_running(false), connect_pending(false), gave_up(false),
                             connect_started_ms(0) {
    ip_config.static_ip = false;
}

void WifiManager::loadIpConfig() {
    Preferences net;
    net.begin(NET_NAMESPACE, false);
    ip_config.static_ip = net.isKey("static") && net.getBool("static", false);
    if (ip_config.static_ip) {
        ip_config.ip = IPAddress(net.getUInt("ip", 0));
        ip_config.mask = IPAddress(net.getUInt("mask", 0));
        ip_config.gateway = IPAddress(net.getUInt("gw", 0));
        ip_config.dns = IPAddress(net.getUInt("dns", 0));
    }
    net.end();
}

// Must run after WiFi.mode() and before WiFi.begin(). An all-zero address switches the
// interface back to DHCP.
void WifiManager::applyIpConfig() {
    if (ip_config.static_ip) {
        WiFi.config(ip_config.ip, ip_config.gateway, ip_config.mask, ip_config.dns);
        Serial.printf("WiFi: static IP %s mask %s gw %s dns %s\n",
                      ip_config.ip.toString().c_str(), ip_config.mask.toString().c_str(),
                      ip_config.gateway.toString().c_str(), ip_config.dns.toString().c_str());
    } else {
        WiFi.config(IPAddress(), IPAddress(), IPAddress());
        Serial.println("WiFi: DHCP");
    }
}

void WifiManager::setIpConfig(const IpConfig& config) {
    ip_config = config;
    Preferences net;
    net.begin(NET_NAMESPACE, false);
    net.putBool("static", config.static_ip);
    net.putUInt("ip", (uint32_t)config.ip);
    net.putUInt("mask", (uint32_t)config.mask);
    net.putUInt("gw", (uint32_t)config.gateway);
    net.putUInt("dns", (uint32_t)config.dns);
    net.end();
    reconnect();
}

IpConfig WifiManager::currentAddress() {
    IpConfig now = ip_config;
    if (connected()) {
        now.ip = WiFi.localIP();
        now.mask = WiFi.subnetMask();
        now.gateway = WiFi.gatewayIP();
        now.dns = WiFi.dnsIP();
    } else if (!ip_config.static_ip) {
        now.ip = now.mask = now.gateway = now.dns = IPAddress();
    }
    return now;
}

// Drops the link and joins the saved network again, picking up new address settings.
void WifiManager::reconnect() {
    prefs.begin(PREF_NAMESPACE, false);
    const bool have_creds = prefs.isKey("ssid");
    const String ssid_saved = have_creds ? prefs.getString("ssid", "") : String("");
    const String pass_saved = have_creds ? prefs.getString("pass", "") : String("");
    prefs.end();
    if (ssid_saved.length() == 0) {
        applyIpConfig();   // takes effect on the next connect
        return;
    }
    connect(ssid_saved, pass_saved);
}

void WifiManager::begin() {
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_STA_CONNECTED:
                Serial.printf("[%lu] WiFi: associated\n", millis());
                break;
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
                fail_count = 0;
                Serial.printf("[%lu] WiFi: got IP %s\n", millis(),
                              WiFi.localIP().toString().c_str());
                break;
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
                // After giving up, the disconnect we issue ourselves raises one more
                // event; don't let it overwrite the reason that actually mattered.
                if (give_up_requested) break;
                last_reason = info.wifi_sta_disconnected.reason;
                if (is_handshake_failure(last_reason)) handshake_failed = true;
                fail_count = fail_count + 1;
                if (fail_count >= MAX_CONNECT_FAILURES) give_up_requested = true;
                Serial.printf("[%lu] WiFi: disconnected, reason %u (%u/%u)\n", millis(),
                              last_reason, fail_count, MAX_CONNECT_FAILURES);
                break;
            default:
                break;
        }
    });

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Radio current peaks during scan and association sag a USB-limited supply enough
    // to disturb the display. Trades range for a steadier rail; raise it if the station
    // runs on a proper 5 V supply and sits far from the AP.
    WiFi.setTxPower(WIFI_POWER_13dBm);

    loadIpConfig();
    applyIpConfig();

    // Read-write so the namespace is created on first boot; opening it read-only
    // before it exists logs an nvs_open NOT_FOUND error.
    prefs.begin(PREF_NAMESPACE, false);
    // Preferences logs an error for a missing key, so check before reading.
    const bool have_creds = prefs.isKey("ssid");
    const String saved_ssid = have_creds ? prefs.getString("ssid", "") : String("");
    const String saved_pass = have_creds ? prefs.getString("pass", "") : String("");
    prefs.end();

    if (saved_ssid.length() > 0) {
        Serial.printf("WiFi: connecting to saved network '%s'\n", saved_ssid.c_str());
        WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
        connect_pending = true;
        connect_started_ms = millis();
    } else {
        Serial.println("WiFi: no saved credentials");
    }
}

void WifiManager::startScan() {
    // Scanning would implicitly power the radio up; only scan once begin() has.
    if (scan_running || WiFi.getMode() == WIFI_OFF) return;
    WiFi.scanDelete();
    WiFi.scanNetworks(true);  // async, so the UI keeps running
    scan_running = true;
}

bool WifiManager::scanning() {
    if (!scan_running) return false;
    if (WiFi.scanComplete() >= 0) scan_running = false;
    return scan_running;
}

int WifiManager::networkCount() {
    const int n = WiFi.scanComplete();
    return n < 0 ? -1 : n;
}

String WifiManager::ssidAt(int index) {
    return WiFi.SSID(index);
}

int WifiManager::rssiAt(int index) {
    return WiFi.RSSI(index);
}

void WifiManager::loop() {
    if (give_up_requested && !gave_up) {
        gave_up = true;
        connect_pending = false;
        WiFi.setAutoReconnect(false);
        WiFi.disconnect();
        Serial.printf("WiFi: giving up after %u failures (reason %u)\n",
                      MAX_CONNECT_FAILURES, last_reason);
    }
}

void WifiManager::connect(const String& ssid_in, const String& password) {
    if (ssid_in.length() == 0) return;

    fail_count = 0;
    give_up_requested = false;
    handshake_failed = false;
    gave_up = false;
    WiFi.setAutoReconnect(true);

    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString("ssid", ssid_in);
    prefs.putString("pass", password);
    prefs.end();

    WiFi.disconnect();
    applyIpConfig();
    WiFi.begin(ssid_in.c_str(), password.c_str());
    connect_pending = true;
    connect_started_ms = millis();
    Serial.printf("WiFi: connecting to '%s'\n", ssid_in.c_str());
}

void WifiManager::forget() {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    WiFi.disconnect();
    connect_pending = false;
}

bool WifiManager::connected() {
    return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ip() {
    return connected() ? WiFi.localIP().toString() : String("0.0.0.0");
}

String WifiManager::ssid() {
    return connected() ? WiFi.SSID() : String("");
}

String WifiManager::statusText() {
    if (connected()) {
        connect_pending = false;
        return "connected to " + WiFi.SSID() + "  " + WiFi.localIP().toString();
    }
    if (gave_up) {
        // Usually a wrong password, but not always: this also fired with a correct one
        // while WiFi buffers were in PSRAM and the handshake frames were delayed.
        if (handshake_failed) return "failed: WPA handshake timed out - check the password";
        if (last_reason == 201) return "failed: network not found";
        return "failed (reason " + String(last_reason) + ")";
    }
    if (connect_pending) {
        if (millis() - connect_started_ms > CONNECT_TIMEOUT_MS) {
            connect_pending = false;
            return "connection failed - check the password";
        }
        return "connecting...";
    }
    return "not connected";
}
