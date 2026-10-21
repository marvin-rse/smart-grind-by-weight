#include "diagnostic_log.h"

#include <cstdarg>
#include <cstring>
#include <esp_attr.h>
#include <esp_heap_caps.h>

namespace {

constexpr size_t DIAGNOSTIC_LOG_CAPACITY = 6144;
constexpr uint32_t RETAINED_LOG_MAGIC = 0x534C4F47;  // "SLOG"

// Kept in RTC slow memory, which is not cleared by a software restart, panic
// or watchdog reset. The log of a failed firmware update therefore survives
// the recovery restart that follows it. A power cycle clears it. RTC memory
// does not reduce the internal heap that Wi-Fi updates depend on.
struct RetainedLog {
    uint32_t magic;
    uint32_t write_position;
    uint32_t retained_size;
    uint32_t check;
    char data[DIAGNOSTIC_LOG_CAPACITY];
};
RTC_NOINIT_ATTR RetainedLog retained_log;

bool initialized = false;  // ordinary RAM: false after every restart
portMUX_TYPE log_mutex = portMUX_INITIALIZER_UNLOCKED;

uint32_t checksum(const RetainedLog& log) {
    return log.magic ^ (log.write_position * 2654435761U) ^ (log.retained_size * 40503U) ^ 0xA5A55A5AU;
}

bool retained_log_valid() {
    return retained_log.magic == RETAINED_LOG_MAGIC &&
           retained_log.write_position < DIAGNOSTIC_LOG_CAPACITY &&
           retained_log.retained_size <= DIAGNOSTIC_LOG_CAPACITY &&
           retained_log.check == checksum(retained_log);
}

// Caller holds log_mutex.
void append_locked(const char* message, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        retained_log.data[retained_log.write_position] = message[i];
        retained_log.write_position = (retained_log.write_position + 1) % DIAGNOSTIC_LOG_CAPACITY;
        if (retained_log.retained_size < DIAGNOSTIC_LOG_CAPACITY) ++retained_log.retained_size;
    }
    retained_log.check = checksum(retained_log);
}

const char* reset_reason_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_EXT: return "reset pin";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        default: return "other";
    }
}

void ensure_initialized() {
    if (!initialized) diagnostic_log_begin(esp_reset_reason());
}

void retain_message(const char* message, size_t length) {
    if (!message || length == 0) return;
    ensure_initialized();
    portENTER_CRITICAL(&log_mutex);
    append_locked(message, length);
    portEXIT_CRITICAL(&log_mutex);
}
}  // namespace

void diagnostic_log_begin(esp_reset_reason_t reason) {
    portENTER_CRITICAL(&log_mutex);
    if (initialized) {
        portEXIT_CRITICAL(&log_mutex);
        return;
    }
    initialized = true;
    const bool keep = reason != ESP_RST_POWERON && reason != ESP_RST_UNKNOWN && retained_log_valid();
    if (!keep) {
        retained_log.magic = RETAINED_LOG_MAGIC;
        retained_log.write_position = 0;
        retained_log.retained_size = 0;
        retained_log.check = checksum(retained_log);
        portEXIT_CRITICAL(&log_mutex);
        return;
    }
    char marker[80];
    const int written = snprintf(marker, sizeof(marker),
                                 "\n===== Restarted (%s); log before the restart is above =====\n",
                                 reset_reason_name(reason));
    if (written > 0) append_locked(marker, static_cast<size_t>(written) < sizeof(marker)
                                               ? static_cast<size_t>(written)
                                               : sizeof(marker) - 1);
    portEXIT_CRITICAL(&log_mutex);
}

void diagnostic_log_write(const char* message) {
    if (!message) return;
    Serial.print(message);
    retain_message(message, strlen(message));
}

void diagnostic_log_printf(const char* format, ...) {
    if (!format) return;
    char message[512];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (written <= 0) return;
    const size_t length = static_cast<size_t>(written) < sizeof(message)
                              ? static_cast<size_t>(written)
                              : sizeof(message) - 1;
    Serial.write(reinterpret_cast<const uint8_t*>(message), length);
    retain_message(message, length);
}

String diagnostic_log_snapshot() {
    ensure_initialized();
    // Copy into PSRAM rather than the caller's stack: the web server task has
    // a limited stack and the log is larger than a typical frame.
    char* snapshot = static_cast<char*>(
        heap_caps_malloc(DIAGNOSTIC_LOG_CAPACITY + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!snapshot) snapshot = static_cast<char*>(malloc(DIAGNOSTIC_LOG_CAPACITY + 1));
    if (!snapshot) return String();
    portENTER_CRITICAL(&log_mutex);
    const size_t size = retained_log.retained_size;
    const size_t start = (retained_log.write_position + DIAGNOSTIC_LOG_CAPACITY - size) %
                         DIAGNOSTIC_LOG_CAPACITY;
    for (size_t i = 0; i < size; ++i) {
        snapshot[i] = retained_log.data[(start + i) % DIAGNOSTIC_LOG_CAPACITY];
    }
    portEXIT_CRITICAL(&log_mutex);
    snapshot[size] = '\0';
    String result(snapshot);
    free(snapshot);
    return result;
}
