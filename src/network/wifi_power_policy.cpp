#include "wifi_power_policy.h"

#include <Preferences.h>
#include <WiFi.h>
#include <mutex>

#include "../config/constants.h"

namespace wifi_power_policy {
namespace {

std::mutex g_mutex;
bool g_power_save_preference = true;
bool g_bluetooth_active = false;

constexpr const char* kNamespace = "wifi";
constexpr const char* kKey = "power_save";

}  // namespace

void init() {
    Preferences preferences;
    bool enabled = true;
    if (preferences.begin(kNamespace, true)) {
        enabled = preferences.getBool(kKey, true);
        preferences.end();
    }
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_power_save_preference = enabled;
}

bool power_save_enabled() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    return g_power_save_preference;
}

void set_power_save_enabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_power_save_preference = enabled;
}

void bluetooth_starting() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_bluetooth_active = true;
    // Stored by the Wi-Fi driver even when the station is not started yet, so
    // a later reconnect cannot bring the radio up without modem sleep.
    WiFi.setSleep(true);
}

void bluetooth_stopped() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    g_bluetooth_active = false;
}

void update(bool wifi_connected) {
    if (!wifi_connected) return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    const bool sleep_required = g_power_save_preference || g_bluetooth_active;
    const wifi_ps_type_t desired = sleep_required ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE;
    if (WiFi.getSleep() == desired) return;
    if (WiFi.setSleep(desired)) {
        LOG_BLE("[WIFI] Modem sleep %s%s\n", sleep_required ? "on" : "off",
                g_bluetooth_active && !g_power_save_preference ? " (required by Bluetooth)" : "");
    }
}

}  // namespace wifi_power_policy
