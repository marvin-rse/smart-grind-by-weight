#pragma once

// Decides whether the Wi-Fi radio may use modem sleep.
//
// Modem sleep lets the radio doze between router beacons, which adds roughly
// 100-300 ms of latency to every exchange with the web interface. The grinder
// is mains powered, so users may turn it off in the web settings.
//
// ESP32 Wi-Fi/Bluetooth coexistence requires modem sleep while the BLE stack
// runs. Bluetooth therefore always forces it back on before the stack starts,
// and the user preference only applies while Bluetooth is off.
namespace wifi_power_policy {

// Loads the stored preference ("wifi" namespace, key "power_save"; default on).
void init();

bool power_save_enabled();
// Applies a preference that the settings API has already persisted.
void set_power_save_enabled(bool enabled);

// Must be called before the BLE stack is initialised.
void bluetooth_starting();
void bluetooth_stopped();

// Called from the network loop; applies the effective mode while connected.
void update(bool wifi_connected);

}  // namespace wifi_power_policy
