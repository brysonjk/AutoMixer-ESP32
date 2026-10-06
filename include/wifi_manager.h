#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <IPAddress.h>

// How the station gets its address. With static_ip false the rest is ignored and DHCP
// is used. Stored in NVS namespace "net", separate from the WiFi credentials, so
// forgetting a network keeps the address settings.
struct IpConfig {
    bool static_ip;
    IPAddress ip;
    IPAddress mask;
    IPAddress gateway;
    IPAddress dns;
};

class WifiManager {
public:
    WifiManager();

    // Loads stored credentials and starts connecting if any exist.
    void begin();

    // Call from the main loop; stops reconnect attempts after repeated failures.
    void loop();

    void startScan();
    bool scanning();
    int networkCount();          // -1 until a scan completes
    String ssidAt(int index);
    int rssiAt(int index);

    // Stores the credentials and starts connecting.
    void connect(const String& ssid, const String& password);
    void forget();

    IpConfig ipConfig() { return ip_config; }
    // Saves the address settings and reconnects with them.
    void setIpConfig(const IpConfig& config);
    // The address actually in use, whichever way it was obtained. 0.0.0.0 if offline.
    IpConfig currentAddress();

    bool connected();
    String ip();
    String ssid();
    String statusText();

private:
    bool scan_running;
    bool connect_pending;
    bool gave_up;
    uint32_t connect_started_ms;
    IpConfig ip_config;

    void loadIpConfig();
    void applyIpConfig();
    void reconnect();
};

#endif
