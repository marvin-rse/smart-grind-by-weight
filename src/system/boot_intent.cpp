#include "boot_intent.h"

#include <esp_attr.h>
#include <cstdint>

namespace boot_intent {
namespace {

constexpr uint32_t kMagic = 0x42495431;  // "BIT1"
constexpr uint32_t kSkipBluetooth = 1u << 0;
constexpr uint32_t kResumeDeviceUpdate = 1u << 1;
constexpr uint32_t kKnownFlags = kSkipBluetooth | kResumeDeviceUpdate;

struct StoredIntent {
    uint32_t magic;
    uint32_t flags;
    uint32_t check;
};
RTC_NOINIT_ATTR StoredIntent stored;

bool evaluated = false;      // ordinary RAM: false after every restart
uint32_t boot_flags = 0;     // instructions that apply to this boot

uint32_t checksum(uint32_t flags) { return kMagic ^ (flags * 0x9E3779B1u) ^ 0x5A5AA5A5u; }

void clear_stored() {
    stored.magic = kMagic;
    stored.flags = 0;
    stored.check = checksum(0);
}

void evaluate(esp_reset_reason_t reason) {
    evaluated = true;
    const bool valid = stored.magic == kMagic && (stored.flags & ~kKnownFlags) == 0 &&
                       stored.check == checksum(stored.flags);
    // Only a deliberate software restart carries instructions forward.
    boot_flags = (valid && reason == ESP_RST_SW) ? stored.flags : 0;
    clear_stored();
}

void ensure_evaluated() {
    if (!evaluated) evaluate(esp_reset_reason());
}

bool take(uint32_t flag) {
    ensure_evaluated();
    const bool set = (boot_flags & flag) != 0;
    boot_flags &= ~flag;
    return set;
}

}  // namespace

void request_update_restart(bool resume_device_update) {
    ensure_evaluated();
    const uint32_t flags = kSkipBluetooth | (resume_device_update ? kResumeDeviceUpdate : 0);
    stored.magic = kMagic;
    stored.flags = flags;
    stored.check = checksum(flags);
}

bool take_skip_bluetooth() { return take(kSkipBluetooth); }
bool take_resume_device_update() { return take(kResumeDeviceUpdate); }

void reset_for_boot(esp_reset_reason_t reason) { evaluate(reason); }

}  // namespace boot_intent
