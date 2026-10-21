#pragma once

#include <Arduino.h>
#include <esp_system.h>

// Writes to the existing USB serial console and retains a bounded recent-log
// snapshot for read-only Wi-Fi diagnostics. The buffer lives in RTC memory, so
// it never adds flash wear and survives software restarts (but not a power
// cycle); a marker line shows where a restart happened.
#if defined(__GNUC__)
#define SMART_GRIND_PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define SMART_GRIND_PRINTF_FORMAT
#endif
// Keeps the previous log after a software restart and clears it after power-on.
// Called on first use automatically; startup may call it explicitly.
void diagnostic_log_begin(esp_reset_reason_t reason);
void diagnostic_log_printf(const char* format, ...) SMART_GRIND_PRINTF_FORMAT;
void diagnostic_log_write(const char* message);
String diagnostic_log_snapshot();
#undef SMART_GRIND_PRINTF_FORMAT
