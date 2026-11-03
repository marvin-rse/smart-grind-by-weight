"""Host test: visible status icons are packed against the right screen edge."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
struct lv_obj_t { bool hidden = false; int32_t x = 999, y = 999; };
constexpr int LV_OBJ_FLAG_HIDDEN = 1, LV_ALIGN_TOP_RIGHT = 2;
bool lv_obj_has_flag(lv_obj_t* o, int) { return o->hidden; }
void lv_obj_align(lv_obj_t* o, int align, int32_t x, int32_t y) { assert(align == LV_ALIGN_TOP_RIGHT); o->x = x; o->y = y; }
class UIManager;
@CONSTANTS@
struct StatusIndicatorController {
    lv_obj_t* ble_status_icon_; lv_obj_t* wifi_status_icon_;
    lv_obj_t* warning_icon_; lv_obj_t* firmware_update_icon_;
    void position_icons();
};
@METHOD@
int main() {
    lv_obj_t ble, wifi, warning, update;
    StatusIndicatorController c{&ble, &wifi, &warning, &update};
    // Bluetooth off: Wi-Fi moves into the corner (the reported problem).
    ble.hidden = true; warning.hidden = true; update.hidden = true;
    c.position_icons();
    assert(wifi.x == -10 && wifi.y == 10);
    // Bluetooth on: Bluetooth takes the corner, Wi-Fi sits next to it.
    ble.hidden = false; c.position_icons();
    assert(ble.x == -10 && wifi.x == -45);
    // All four visible: one slot each, right to left.
    warning.hidden = false; update.hidden = false; c.position_icons();
    assert(ble.x == -10 && wifi.x == -45 && warning.x == -80 && update.x == -115);
    // A warning without Bluetooth closes the gap as well.
    ble.hidden = true; update.hidden = true; c.position_icons();
    assert(wifi.x == -10 && warning.x == -45);
    std::puts("status icon layout tests passed");
}
'''


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class StatusIconLayoutTest(unittest.TestCase):
    def test_icons_pack_to_the_right(self):
        source = (ROOT / "src/ui/controllers/status_indicator_controller.cpp").read_text()
        constants = "\n".join(re.findall(r"constexpr int32_t STATUS_ICON_\w+ = \d+;", source))
        method = function(source, "void StatusIndicatorController::position_icons()")
        harness = HARNESS.replace("@CONSTANTS@", constants).replace("@METHOD@", method)
        with tempfile.TemporaryDirectory() as directory:
            cpp, binary = Path(directory) / "icons.cpp", Path(directory) / "icons"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
