// Browser simulator: no WiFi and no web server. The Network page still works, against
// made-up networks, and nothing real (network names, addresses, PIN) is involved.
#include "web_dashboard.h"
#include "wifi_manager.h"

WifiManager::WifiManager() : scan_running(false), connect_pending(false), gave_up(false),
                             connect_started_ms(0) {
    ip_config.static_ip = false;
}

void WifiManager::begin() { Serial.println("WiFi: simulated (demo)"); }
void WifiManager::loop() {}

void WifiManager::startScan() {
    scan_running = true;
    connect_started_ms = millis();
}
bool WifiManager::scanning() {
    if (scan_running && millis() - connect_started_ms > 1500) scan_running = false;
    return scan_running;
}
int WifiManager::networkCount() { return scan_running ? -1 : 3; }
String WifiManager::ssidAt(int index) {
    static const char* names[] = {"DiveShop-Demo", "Compressor-Room", "Guest"};
    return String(names[index % 3]);
}
int WifiManager::rssiAt(int index) { return -48 - index * 11; }

void WifiManager::connect(const String&, const String&) { gave_up = true; }
void WifiManager::forget() { gave_up = false; }
bool WifiManager::connected() { return false; }
String WifiManager::ip() { return String("0.0.0.0"); }
String WifiManager::ssid() { return String(""); }
String WifiManager::statusText() {
    return gave_up ? String("demo: WiFi is not available in this demo") : String("not connected (demo)");
}
void WifiManager::setIpConfig(const IpConfig& config) { ip_config = config; }
IpConfig WifiManager::currentAddress() { return ip_config; }

WebDashboard::WebDashboard(OxygenSensor* s, ValveController* v, FillStationGUI* g)
    : sensors(s), valves(v), gui(g), last_publish_ms(0), failed_pins(0), locked_until_ms(0) {}
void WebDashboard::begin() {}
void WebDashboard::loop() {}
