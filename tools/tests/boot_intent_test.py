"""Host test: one-shot boot instructions survive only a software restart."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "system/boot_intent.h"
using namespace boot_intent;
int main() {
    reset_for_boot(ESP_RST_POWERON);
    assert(!take_skip_bluetooth() && !take_resume_device_update());
    request_update_restart(false);
    reset_for_boot(ESP_RST_SW);
    assert(take_skip_bluetooth() && !take_skip_bluetooth());   // only once
    assert(!take_resume_device_update());
    reset_for_boot(ESP_RST_SW);                                // consumed: not again
    assert(!take_skip_bluetooth());
    request_update_restart(true);
    reset_for_boot(ESP_RST_SW);
    assert(take_resume_device_update() && take_skip_bluetooth());
    // Any other reset (power, crash, watchdog) discards instructions.
    for (esp_reset_reason_t reason : {ESP_RST_POWERON, ESP_RST_PANIC, ESP_RST_TASK_WDT, ESP_RST_BROWNOUT}) {
        request_update_restart(true);
        reset_for_boot(reason);
        assert(!take_skip_bluetooth() && !take_resume_device_update());
    }
    std::puts("boot intent tests passed");
}
'''


class BootIntentTest(unittest.TestCase):
    def test_one_shot_instructions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "system").mkdir()
            for name in ("boot_intent.h", "boot_intent.cpp"):
                (root / "system" / name).write_text((ROOT / "src/system" / name).read_text())
            (root / "esp_attr.h").write_text("#pragma once\n#define RTC_NOINIT_ATTR\n")
            (root / "esp_system.h").write_text("""#pragma once
typedef enum { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC,
               ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP,
               ESP_RST_BROWNOUT, ESP_RST_SDIO } esp_reset_reason_t;
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
""")
            (root / "main.cpp").write_text(HARNESS)
            binary = root / "intent"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-I", str(root), str(root / "main.cpp"), str(root / "system" / "boot_intent.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
