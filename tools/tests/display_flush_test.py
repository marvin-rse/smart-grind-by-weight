"""Host test for the V2 display flush path (staged PSRAM frame -> DMA strips).

The panel is simulated with asynchronous transfers: a strip is only "sent"
(its buffer contents captured) when the flush code waits for completion. This
catches strip buffers being overwritten before the panel has read them, rows
being skipped or duplicated, and LVGL being told a flush is done too early,
too late or more than once.
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


STUBS = {
    "config/hardware.h": "#pragma once\n#define HW_DISPLAY_VARIANT_V2 1\n#define HW_DISPLAY_OFFSET_X_PX 20\n",
    "config/constants.h": "#pragma once\n",
    "hardware/touch_driver.h": """#pragma once
#include <cstdint>
struct TouchData { uint16_t x, y; bool pressed, just_pressed; };
class TouchDriver {};
""",
    "esp_lcd_panel_io.h": """#pragma once
#include <cstdint>
#include <cstddef>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef struct panel_io* esp_lcd_panel_io_handle_t;
typedef struct { int unused; } esp_lcd_panel_io_event_data_t;
""",
    "esp_lcd_panel_ops.h": """#pragma once
typedef struct panel* esp_lcd_panel_handle_t;
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void*);
""",
    "lvgl.h": """#pragma once
#include <cstdint>
struct lv_display_t { int ready_calls = 0; };
struct lv_indev_t {};
struct lv_event_t {};
struct lv_indev_data_t {};
typedef uint16_t lv_color_t;
struct lv_area_t { int32_t x1, y1, x2, y2; };
inline int32_t lv_area_get_width(const lv_area_t* a) { return a->x2 - a->x1 + 1; }
inline int32_t lv_area_get_height(const lv_area_t* a) { return a->y2 - a->y1 + 1; }
inline void lv_display_flush_ready(lv_display_t* d) { ++d->ready_calls; }
""",
}

HARNESS = r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include <algorithm>
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;
typedef int BaseType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
struct portMUX_TYPE { int unused; };
#define portMUX_INITIALIZER_UNLOCKED {0}
template<class... T> void ignore_log(T...) {}
#define LOG_BLE(...) ignore_log(__VA_ARGS__)
uint32_t fake_ms = 0;
uint32_t millis() { return fake_ms; }
uint32_t micros() { return fake_ms * 1000; }
#include "hardware/display_manager.h"

DisplayManager* g_display_manager = nullptr;
DisplayManager manager;

struct Transfer { int x1, y1, x2, y2; const uint16_t* pixels; size_t count; };
std::deque<Transfer> in_flight;
std::vector<std::vector<uint16_t>> panel;   // what the panel received, per row
int fail_on_transfer = -1, transfers_started = 0;
bool panel_stalled = false;

void complete_one() {
    Transfer t = in_flight.front(); in_flight.pop_front();
    const int width = t.x2 - t.x1;
    for (int row = t.y1; row < t.y2; ++row) {
        panel[row].assign(t.pixels + (row - t.y1) * width, t.pixels + (row - t.y1 + 1) * width);
    }
    DisplayManager::color_transfer_done_cb(nullptr, nullptr, &manager);
}

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int x1, int y1, int x2, int y2, const void* data) {
    if (transfers_started++ == fail_on_transfer) return ESP_FAIL;
    in_flight.push_back({x1, y1, x2, y2, static_cast<const uint16_t*>(data), size_t((x2 - x1) * (y2 - y1))});
    return ESP_OK;
}
int xSemaphoreTake(SemaphoreHandle_t, int ticks) {
    // Time passes while waiting: the panel finishes the oldest transfer.
    if (!panel_stalled && !in_flight.empty()) complete_one(); else fake_ms += ticks;
    return 1;
}
void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*) {}
''' + "\n".join(["@METHODS@"]) + r'''

static std::vector<uint16_t> frame(int width, int height, uint16_t seed) {
    std::vector<uint16_t> pixels(width * height);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint16_t(seed + i * 7);
    return pixels;
}

static void run_flush(bool staged, int y1, int height, int fail_at = -1, bool stall = false) {
    const int width = 280;
    uint16_t strip_a[280 * 8], strip_b[280 * 8];
    manager.panel_handle = reinterpret_cast<esp_lcd_panel_handle_t>(1);
    manager.staged_flush = staged;
    manager.dma_strip_rows = 8;
    manager.dma_strips[0] = strip_a; manager.dma_strips[1] = strip_b;
    manager.transfer_done_signal = reinterpret_cast<SemaphoreHandle_t>(1);
    in_flight.clear(); panel.assign(456, {}); transfers_started = 0;
    fail_on_transfer = fail_at; panel_stalled = stall;
    lv_display_t display;
    lv_area_t area{0, y1, width - 1, y1 + height - 1};
    std::vector<uint16_t> pixels = frame(width, height, uint16_t(y1 + height));
    DisplayManager::display_flush_cb(&display, &area, reinterpret_cast<uint8_t*>(pixels.data()));
    if (fail_at >= 0 || stall) {
        assert(display.ready_calls == 1);       // LVGL is released even on failure
        panel_stalled = false;
        while (!in_flight.empty()) complete_one();
        assert(display.ready_calls == 1);       // late completions do not report again
        return;
    }
    // At most the two strip buffers can be in flight; LVGL is not told before they complete.
    assert(in_flight.size() <= 2);
    if (!in_flight.empty()) assert(display.ready_calls == 0);
    while (!in_flight.empty()) complete_one();
    assert(display.ready_calls == 1);
    for (int row = 0; row < height; ++row) {
        const std::vector<uint16_t> expected(pixels.begin() + row * width, pixels.begin() + (row + 1) * width);
        assert(panel[y1 + row] == expected);
    }
}

int main() {
    g_display_manager = &manager;
    for (bool staged : {true, false}) {
        if (!staged) {
            // Direct mode renders straight into one DMA buffer of at most 8 rows.
            run_flush(false, 0, 8); run_flush(false, 100, 3);
            continue;
        }
        run_flush(true, 0, 456);     // full frame, 57 strips
        run_flush(true, 13, 1);      // single row
        run_flush(true, 40, 8);      // exactly one strip
        run_flush(true, 40, 17);     // partial last strip
        run_flush(true, 100, 16);    // exactly two strips
    }
    run_flush(true, 0, 456, 0);      // first strip rejected
    run_flush(true, 0, 456, 5);      // a middle strip rejected
    run_flush(true, 0, 456, -1, true);  // panel stops answering: timeout releases LVGL
    assert(manager.metrics_window.flushes > 0);
    std::puts("display flush tests passed");
}
'''


class DisplayFlushTest(unittest.TestCase):
    def test_staged_v2_flush(self):
        source = (ROOT / "src/hardware/display_manager.cpp").read_text()
        methods = "\n".join(function(source, signature) for signature in (
            "void DisplayManager::display_flush_cb(",
            "bool DisplayManager::color_transfer_done_cb(",
            "bool DisplayManager::queue_v2_transfer(",
            "bool DisplayManager::wait_for_v2_transfers(",
        ))
        constants = ("constexpr uint32_t kTransferTimeoutMs = 250;\n")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in STUBS.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            (root / "hardware" / "display_manager.h").write_text(
                (ROOT / "src/hardware/display_manager.h").read_text().replace("private:", "public:"))
            (root / "main.cpp").write_text(HARNESS.replace("@METHODS@", constants + methods))
            binary = root / "flush"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                            "-I", str(root), str(root / "main.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=60)


if __name__ == "__main__":
    unittest.main()
