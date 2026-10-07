// Browser simulator: NVS as an in-memory map. Settings last until the page reloads.
#pragma once
#include <map>
#include <vector>
#include "Arduino.h"

class Preferences {
public:
    bool begin(const char* ns, bool = false) { ns_ = ns; return true; }
    void end() {}
    bool isKey(const char* key) { return space().count(key) != 0; }
    bool remove(const char* key) { return space().erase(key) != 0; }
    bool clear() { space().clear(); return true; }

    size_t putFloat(const char* k, float v) { return put(k, &v, sizeof v); }
    float getFloat(const char* k, float d = 0) { get(k, &d, sizeof d); return d; }
    size_t putBool(const char* k, bool v) { return put(k, &v, sizeof v); }
    bool getBool(const char* k, bool d = false) { get(k, &d, sizeof d); return d; }
    size_t putUChar(const char* k, uint8_t v) { return put(k, &v, sizeof v); }
    uint8_t getUChar(const char* k, uint8_t d = 0) { get(k, &d, sizeof d); return d; }
    size_t putUInt(const char* k, uint32_t v) { return put(k, &v, sizeof v); }
    uint32_t getUInt(const char* k, uint32_t d = 0) { get(k, &d, sizeof d); return d; }
    size_t putString(const char* k, const char* v) { return put(k, v, std::strlen(v) + 1); }
    size_t putString(const char* k, const String& v) { return putString(k, v.c_str()); }
    String getString(const char* k, const String& d = String()) {
        auto it = space().find(k);
        return it == space().end() ? d : String((const char*)it->second.data());
    }
    size_t putBytes(const char* k, const void* v, size_t n) { return put(k, v, n); }
    size_t getBytes(const char* k, void* buf, size_t n) {
        auto it = space().find(k);
        if (it == space().end()) return 0;
        const size_t len = it->second.size() < n ? it->second.size() : n;
        std::memcpy(buf, it->second.data(), len);
        return len;
    }

    // Lets the simulator seed values before the firmware starts (sim_main.cpp).
    static std::map<std::string, std::map<std::string, std::vector<uint8_t>>>& store() {
        static std::map<std::string, std::map<std::string, std::vector<uint8_t>>> s;
        return s;
    }

private:
    std::string ns_;
    std::map<std::string, std::vector<uint8_t>>& space() { return store()[ns_]; }
    size_t put(const char* k, const void* v, size_t n) {
        auto& b = space()[k];
        b.assign((const uint8_t*)v, (const uint8_t*)v + n);
        return n;
    }
    void get(const char* k, void* out, size_t n) {
        auto it = space().find(k);
        if (it != space().end() && it->second.size() == n) std::memcpy(out, it->second.data(), n);
    }
};
