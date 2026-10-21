#pragma once

#include <esp_system.h>

// One-shot instructions for the next boot, kept in RTC memory across a
// software restart. A power cycle or any other reset clears them.
//
// Used to prepare firmware updates: stopping Bluetooth in a running system
// left too little headroom for reliable transfers, while updates after a boot
// without Bluetooth succeed. When an update is requested with Bluetooth
// active, the grinder restarts once without Bluetooth and continues there.
namespace boot_intent {

// Schedules a boot without Bluetooth. With resume_device_update the grinder
// also restarts the on-device update ("Install update" in the menu) after it
// has checked for the latest release again.
void request_update_restart(bool resume_device_update);

// Each returns true at most once per boot, then clears the instruction.
bool take_skip_bluetooth();
bool take_resume_device_update();

// For tests: re-evaluate the stored state as if the system had just booted.
void reset_for_boot(esp_reset_reason_t reason);

}  // namespace boot_intent
