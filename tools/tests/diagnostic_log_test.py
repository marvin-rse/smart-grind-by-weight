"""Host test: the diagnostic log survives software restarts but not power-on."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

STUBS = {
    "Arduino.h": """#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <string>
struct String : std::string { using std::string::string; String() = default; String(const char* s) : std::string(s) {} };
struct { void print(const char*) {} void write(const uint8_t*, size_t) {} } Serial;
struct portMUX_TYPE { int unused; };
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)
""",
    "esp_system.h": """#pragma once
typedef enum { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC,
               ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP,
               ESP_RST_BROWNOUT, ESP_RST_SDIO } esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
""",
    "esp_attr.h": "#pragma once\n#define RTC_NOINIT_ATTR\n",
    "esp_heap_caps.h": """#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t n, int) { return std::malloc(n); }
""",
}

HARNESS = r'''
#include <cassert>
#include <cstring>
#include <string>
#include "logging/diagnostic_log.h"
// Simulate a restart: ordinary RAM (the "initialized" flag) is reset, RTC memory is not.
#define private public
#include "logging/diagnostic_log.cpp"
static void restart(esp_reset_reason_t reason) { initialized = false; diagnostic_log_begin(reason); }

int main() {
    restart(ESP_RST_POWERON);
    assert(diagnostic_log_snapshot().empty());
    diagnostic_log_printf("[WEB OTA] Download incomplete (%d/%d)\n", 100, 200);
    restart(ESP_RST_SW);
    std::string log = diagnostic_log_snapshot();
    assert(log.find("Download incomplete (100/200)") != std::string::npos);
    assert(log.find("Restarted (software restart)") != std::string::npos);
    diagnostic_log_write("[STARTUP] after restart\n");
    restart(ESP_RST_PANIC);
    log = diagnostic_log_snapshot();
    assert(log.find("Download incomplete") != std::string::npos);
    assert(log.find("Restarted (crash)") != std::string::npos);
    // Power-on clears; so does corrupted retained memory.
    restart(ESP_RST_POWERON);
    assert(diagnostic_log_snapshot().empty());
    diagnostic_log_write("keep me\n");
    retained_log.check ^= 1;
    restart(ESP_RST_SW);
    assert(diagnostic_log_snapshot().empty());
    retained_log.write_position = DIAGNOSTIC_LOG_CAPACITY + 5; retained_log.check = checksum(retained_log);
    restart(ESP_RST_SW);
    assert(diagnostic_log_snapshot().empty());
    // Wrap-around keeps exactly the newest bytes.
    restart(ESP_RST_POWERON);
    std::string expected;
    for (int i = 0; i < 2000; ++i) {
        const std::string line = "line " + std::to_string(i) + "\n";
        diagnostic_log_write(line.c_str()); expected += line;
    }
    log = diagnostic_log_snapshot();
    assert(log.size() == DIAGNOSTIC_LOG_CAPACITY);
    assert(log == expected.substr(expected.size() - DIAGNOSTIC_LOG_CAPACITY));
    std::puts("diagnostic log tests passed");
}
'''


class DiagnosticLogTest(unittest.TestCase):
    def test_log_survives_software_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "logging").mkdir()
            for name in ("diagnostic_log.h", "diagnostic_log.cpp"):
                (root / "logging" / name).write_text((ROOT / "src/logging" / name).read_text())
            for name, text in STUBS.items():
                (root / name).write_text(text)
            (root / "main.cpp").write_text(HARNESS)
            binary = root / "log"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(root), str(root / "main.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
