#include "display_manager.h"
#include "touch_wake_policy.h"
#if HW_DISPLAY_VARIANT_V2
#include "esp_lcd_sh8601.h"
#endif
#include "../config/constants.h"
#include "../config/logging.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#if HW_DISPLAY_VARIANT_V2
#include <driver/spi_master.h>
#endif

DisplayManager* g_display_manager = nullptr;

#if HW_DISPLAY_VARIANT_V2
namespace {
constexpr spi_host_device_t kDisplaySpiHost = SPI2_HOST;

static const uint8_t kCmdC4[] = {0x80};
static const uint8_t kCmd35[] = {0x00};
static const uint8_t kCmd53[] = {0x20};
static const uint8_t kCmd63[] = {0xFF};
static const uint8_t kBrightnessOff[] = {0x00};
static const sh8601_lcd_init_cmd_t kV2InitCommands[] = {
    {0x11, nullptr, 0, 80},
    {0xC4, kCmdC4, sizeof(kCmdC4), 0},
    {0x35, kCmd35, sizeof(kCmd35), 0},
    {0x53, kCmd53, sizeof(kCmd53), 1},
    {0x63, kCmd63, sizeof(kCmd63), 1},
    {0x51, kBrightnessOff, sizeof(kBrightnessOff), 1},
    // Brightness stays at zero here: the panel RAM is still uninitialised and
    // would flash green. DisplayManager raises it after the first LVGL frame.
    {0x29, nullptr, 0, 10},
};

// Fallback so a missing first frame can never leave the screen dark forever.
constexpr uint32_t kStartupBlankTimeoutMs = 3000;

// Height of each internal DMA strip: 280 * 8 * 2 = 4,480 bytes. Two strips are
// used in turn, so internal RAM use stays small for Wi-Fi updates and the UI task.
constexpr uint32_t kDmaStripRows = 8;
// Longest wait for the panel to accept a strip before a flush is abandoned.
constexpr uint32_t kTransferTimeoutMs = 250;
}
#endif

void DisplayManager::init() {
    g_display_manager = this;
    panel_powered_on = true;
    consume_wake_touch_until_release = false;
    wake_touch_guard_started_ms = 0;
    
#if HW_DISPLAY_VARIANT_V2
    // V2 uses an SH8601 panel and GPIO 46 for chip select. The native
    // esp_lcd driver is required; the V1 Arduino_GFX path leaves V2 black.
    spi_bus_config_t bus_config = SH8601_PANEL_BUS_QSPI_CONFIG(
        HW_DISPLAY_SCK_PIN, HW_DISPLAY_D0_PIN, HW_DISPLAY_D1_PIN,
        HW_DISPLAY_D2_PIN, HW_DISPLAY_D3_PIN,
        HW_DISPLAY_WIDTH_PX * HW_DISPLAY_HEIGHT_PX * sizeof(uint16_t));
    if (spi_bus_initialize(kDisplaySpiHost, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        return;
    }

    esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(HW_DISPLAY_CS_PIN, color_transfer_done_cb, this);
    io_config.pclk_hz = HW_DISPLAY_QSPI_FREQUENCY_HZ;
    if (esp_lcd_new_panel_io_spi(
            static_cast<esp_lcd_spi_bus_handle_t>(kDisplaySpiHost),
            &io_config, &panel_io) != ESP_OK) {
        return;
    }

    sh8601_vendor_config_t vendor_config = {};
    vendor_config.init_cmds = kV2InitCommands;
    vendor_config.init_cmds_size = sizeof(kV2InitCommands) / sizeof(kV2InitCommands[0]);
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = HW_DISPLAY_RESET_PIN;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;

    if (esp_lcd_new_panel_sh8601(panel_io, &panel_config, &panel_handle) != ESP_OK ||
        esp_lcd_panel_reset(panel_handle) != ESP_OK ||
        esp_lcd_panel_init(panel_handle) != ESP_OK ||
        esp_lcd_panel_disp_on_off(panel_handle, true) != ESP_OK) {
        return;
    }
    startup_blank_started_ms = millis();
#else
    // Initialize V1 display hardware
    bus = new Arduino_ESP32QSPI(
        HW_DISPLAY_CS_PIN, HW_DISPLAY_SCK_PIN, HW_DISPLAY_D0_PIN, 
        HW_DISPLAY_D1_PIN, HW_DISPLAY_D2_PIN, HW_DISPLAY_D3_PIN);
    
    gfx_device = new Arduino_CO5300(
        bus, HW_DISPLAY_RESET_PIN, HW_DISPLAY_ROTATION_DEG, HW_DISPLAY_WIDTH_PX, HW_DISPLAY_HEIGHT_PX,
        HW_DISPLAY_COLOR_ORDER, HW_DISPLAY_OFFSET_X_PX, HW_DISPLAY_IPS_INVERT_X, HW_DISPLAY_IPS_INVERT_Y);

    
    if (!gfx_device->begin()) {
        return;
    }
    
    gfx_device->fillScreen(RGB565_BLACK);
#endif
    
    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(millis_cb);
    
#if HW_DISPLAY_VARIANT_V2
    screen_width = HW_DISPLAY_WIDTH_PX;
    screen_height = HW_DISPLAY_HEIGHT_PX;
#else
    screen_width = gfx_device->width();
    screen_height = gfx_device->height();
#endif

    // Full screen buffer, but only partial updates used
    // RGB565 format (16bit per pixel)
    draw_buffer = nullptr;
#if HW_DISPLAY_VARIANT_V2
    transfer_done_signal = xSemaphoreCreateBinary();
    if (!transfer_done_signal || !allocate_v2_buffers()) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate V2 display buffers\n");
        return;
    }
#else
    dma_staging_buffer = nullptr;
    dma_staging_rows = 16;

    const size_t draw_rows = 40; // 280 * 40 * 2 = 22,400 bytes
    buffer_size = screen_width * draw_rows * sizeof(uint16_t);

    draw_buffer = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                buffer_size,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!draw_buffer) {
        draw_buffer = static_cast<lv_color_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    buffer_size,
                                    MALLOC_CAP_8BIT));
    }
#endif

    if (!draw_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate LVGL draw buffer\n");
        return;
    }

#if !HW_DISPLAY_VARIANT_V2
    while (dma_staging_rows >= 4 && dma_staging_buffer == nullptr) {
        dma_staging_buffer = static_cast<uint16_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    screen_width * dma_staging_rows * sizeof(uint16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (!dma_staging_buffer) {
            dma_staging_rows /= 2;
        }
    }

    if (!dma_staging_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate DMA staging buffer\n");
        heap_caps_free(draw_buffer);
        draw_buffer = nullptr;
        return;
    }
#endif

    lvgl_display = lv_display_create(screen_width, screen_height);
    lv_display_set_flush_cb(lvgl_display, display_flush_cb);
#if HW_DISPLAY_VARIANT_V2
    lv_display_set_buffers(lvgl_display, draw_buffer, draw_buffer2,
                          buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
#else
    lv_display_set_buffers(lvgl_display, draw_buffer, NULL,
                          buffer_size , LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    lv_display_add_event_cb(lvgl_display, display_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(lvgl_display, display_metrics_cb, LV_EVENT_ALL, NULL);
    
    // Initialize touch
    touch_driver.init();
    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);
    
    initialized = true;
}

void DisplayManager::update() {
    if (!initialized) return;

    const uint32_t started_us = micros();
    touch_driver.update();
    lv_timer_handler();
#if HW_DISPLAY_VARIANT_V2
    present_startup_frame_if_ready();
#endif

    const uint32_t now_ms = millis();
    metrics_window.ui_calls++;
    metrics_window.ui_time_us += micros() - started_us;
    if (metrics_window_started_ms == 0) {
        metrics_window_started_ms = now_ms;
    } else if (now_ms - metrics_window_started_ms >= 1000) {
        metrics_window.window_ms = now_ms - metrics_window_started_ms;
        portENTER_CRITICAL(&metrics_mux);
        metrics_snapshot = metrics_window;
        portEXIT_CRITICAL(&metrics_mux);
        metrics_window = {};
        metrics_window_started_ms = now_ms;
    }
}

DisplayPerformanceSnapshot DisplayManager::get_performance_snapshot() {
    portENTER_CRITICAL(&metrics_mux);
    const DisplayPerformanceSnapshot snapshot = metrics_snapshot;
    portEXIT_CRITICAL(&metrics_mux);
    return snapshot;
}

bool DisplayManager::draw_rgb565_file(const char* path, uint16_t width, uint16_t height) {
#if HW_DISPLAY_VARIANT_V2
    // The SH8601 path submits transfers asynchronously.  The normal LVGL
    // startup screensaver remains available on V2, but this early, direct
    // Arduino_GFX optimisation is V1-only until it has a dedicated transfer
    // completion path.  Do not reference the V1-only gfx_device here: doing so
    // makes the V2 firmware fail to compile.
    (void)path;
    (void)width;
    (void)height;
    return false;
#else
    if (!initialized || !gfx_device || !path || width == 0 || height == 0) {
        return false;
    }

    const size_t expected_size = static_cast<size_t>(width) * height * sizeof(uint16_t);
    File file = LittleFS.open(path, "r");
    if (!file) {
        LOG_BLE("[DISPLAY] RGB565 draw skipped: failed to open %s\n", path);
        return false;
    }

    if (file.size() != expected_size) {
        LOG_BLE("[DISPLAY] RGB565 draw skipped: %s size %u != %u\n",
                path,
                static_cast<unsigned>(file.size()),
                static_cast<unsigned>(expected_size));
        file.close();
        return false;
    }

    uint16_t rows_per_chunk = 16;
    uint16_t* row_buffer = nullptr;
    while (rows_per_chunk >= 4 && row_buffer == nullptr) {
        row_buffer = static_cast<uint16_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    static_cast<size_t>(width) * rows_per_chunk * sizeof(uint16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (!row_buffer) {
            rows_per_chunk /= 2;
        }
    }

    if (!row_buffer) {
        LOG_BLE("[DISPLAY] RGB565 draw skipped: row buffer allocation failed\n");
        file.close();
        return false;
    }

    const int16_t x = static_cast<int16_t>((screen_width > width) ? ((screen_width - width) / 2) : 0);
    const int16_t y = static_cast<int16_t>((screen_height > height) ? ((screen_height - height) / 2) : 0);
    bool success = true;

    for (uint16_t current_y = 0; current_y < height; current_y += rows_per_chunk) {
        const uint16_t rows = std::min<uint16_t>(rows_per_chunk, height - current_y);
        const size_t bytes_to_read = static_cast<size_t>(width) * rows * sizeof(uint16_t);
        const size_t bytes_read = file.read(reinterpret_cast<uint8_t*>(row_buffer), bytes_to_read);
        if (bytes_read != bytes_to_read) {
            LOG_BLE("[DISPLAY] RGB565 draw aborted: short read at row %u\n", current_y);
            success = false;
            break;
        }

        // Screensaver uploads are stored as native little-endian RGB565.
        // Arduino_GFX converts native pixels to the panel byte order here.
        gfx_device->draw16bitRGBBitmap(x, y + current_y, row_buffer, width, rows);
    }

    heap_caps_free(row_buffer);
    file.close();
    return success;
#endif
}

// Update the refresh area to be full width
// This avoids weird artifacts when partial row updates are used
void DisplayManager::display_rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    
    area->x1 = 0;
    area->x2 = g_display_manager->screen_width - 1;
}

void DisplayManager::display_metrics_cb(lv_event_t* e) {
    if (!g_display_manager) return;

    switch (lv_event_get_code(e)) {
        case LV_EVENT_REFR_START:
            g_display_manager->metrics_window.refreshes++;
            break;
        case LV_EVENT_RENDER_START:
            g_display_manager->metrics_window.rendered_frames++;
            g_display_manager->render_started_us = micros();
            break;
        case LV_EVENT_RENDER_READY:
#if HW_DISPLAY_VARIANT_V2
            g_display_manager->startup_frame_rendered = true;
#endif
            if (g_display_manager->render_started_us != 0) {
                g_display_manager->metrics_window.render_time_us +=
                    micros() - g_display_manager->render_started_us;
                g_display_manager->render_started_us = 0;
            }
            break;
        default:
            break;
    }
}

void DisplayManager::display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
#if HW_DISPLAY_VARIANT_V2
    DisplayManager* manager = g_display_manager;
    if (!manager || !manager->panel_handle) {
        lv_display_flush_ready(disp);
        return;
    }

    const uint32_t flush_started_us = micros();
    const int32_t x1 = area->x1 + HW_DISPLAY_OFFSET_X_PX;
    const int32_t x2 = area->x2 + HW_DISPLAY_OFFSET_X_PX + 1;
    const uint32_t width = lv_area_get_width(area);
    const uint32_t height = lv_area_get_height(area);
    const uint32_t strip_rows = manager->staged_flush ? manager->dma_strip_rows : height;
    const uint32_t strips = (height + strip_rows - 1) / strip_rows;

    // The transfer-complete interrupt reports the flush to LVGL once the last
    // strip has been sent.
    manager->transfers_done.store(0);
    manager->transfers_expected.store(strips);
    manager->pending_flush_display = disp;

    uint32_t queued = 0;
    bool ok = true;
    if (!manager->staged_flush) {
        ok = manager->queue_v2_transfer(x1, area->y1, x2, area->y2 + 1, px_map);
        queued = ok ? 1 : 0;
    } else {
        const uint16_t* source = reinterpret_cast<const uint16_t*>(px_map);
        for (uint32_t strip = 0; strip < strips; ++strip) {
            // Strip buffer (strip % 2) was last used two strips ago.
            if (strip >= 2 && !manager->wait_for_v2_transfers(strip - 1)) {
                ok = false;
                break;
            }
            const uint32_t first_row = strip * strip_rows;
            const uint32_t rows = std::min(strip_rows, height - first_row);
            uint16_t* buffer = manager->dma_strips[strip & 1];
            memcpy(buffer, source + static_cast<size_t>(first_row) * width,
                   static_cast<size_t>(rows) * width * sizeof(uint16_t));
            if (!manager->queue_v2_transfer(x1, area->y1 + first_row, x2,
                                            area->y1 + first_row + rows, buffer)) {
                ok = false;
                break;
            }
            ++queued;
        }
    }

    if (!ok) {
        // Never leave LVGL waiting for strips that were not queued.
        manager->pending_flush_display = nullptr;
        manager->wait_for_v2_transfers(queued);
        lv_display_flush_ready(disp);
    }

    manager->metrics_window.flushes++;
    manager->metrics_window.pixels += width * height;
    manager->metrics_window.flush_time_us += micros() - flush_started_us;
#else
    if (!g_display_manager || !g_display_manager->gfx_device) return;

    const uint32_t flush_started_us = micros();
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    uint32_t remaining_rows = h;
    uint32_t current_y = area->y1;
    uint32_t src_row_offset = 0;
    uint16_t* staging = g_display_manager->dma_staging_buffer;
    uint32_t staging_rows = g_display_manager->dma_staging_rows ? g_display_manager->dma_staging_rows : h;
    const uint16_t* src_pixels = reinterpret_cast<const uint16_t*>(px_map);

    while (remaining_rows > 0) {
        uint32_t rows = std::min<uint32_t>(remaining_rows, staging_rows);
        size_t copy_pixels = static_cast<size_t>(w) * rows;
        const uint16_t* chunk_src = src_pixels + (static_cast<size_t>(src_row_offset) * w);

        if (staging) {
            memcpy(staging, chunk_src, copy_pixels * sizeof(uint16_t));

            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, staging, w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, staging, w, rows);
            }
        } else {
            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            }
        }

        remaining_rows -= rows;
        src_row_offset += rows;
        current_y += rows;
    }

    g_display_manager->metrics_window.flushes++;
    g_display_manager->metrics_window.pixels += w * h;
    g_display_manager->metrics_window.flush_time_us += micros() - flush_started_us;

    lv_display_flush_ready(disp);
#endif
}

#if HW_DISPLAY_VARIANT_V2
bool DisplayManager::color_transfer_done_cb(esp_lcd_panel_io_handle_t,
                                            esp_lcd_panel_io_event_data_t*,
                                            void* user_context) {
    DisplayManager* manager = static_cast<DisplayManager*>(user_context);
    if (!manager) return false;
    BaseType_t woken = pdFALSE;
    const uint32_t done = manager->transfers_done.fetch_add(1) + 1;
    if (manager->transfer_done_signal) {
        xSemaphoreGiveFromISR(manager->transfer_done_signal, &woken);
    }
    lv_display_t* display = manager->pending_flush_display;
    if (display && done >= manager->transfers_expected.load()) {
        manager->pending_flush_display = nullptr;
        lv_display_flush_ready(display);
    }
    return woken == pdTRUE;
}

bool DisplayManager::allocate_v2_buffers() {
    const size_t strip_bytes = screen_width * kDmaStripRows * sizeof(uint16_t);
    const size_t frame_bytes = screen_width * screen_height * sizeof(uint16_t);
    for (uint16_t*& strip : dma_strips) {
        strip = static_cast<uint16_t*>(heap_caps_aligned_alloc(
            LV_DRAW_BUF_ALIGN, strip_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    }
    auto* frame_a = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto* frame_b = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (dma_strips[0] && dma_strips[1] && frame_a && frame_b) {
        staged_flush = true;
        dma_strip_rows = kDmaStripRows;
        draw_buffer = frame_a;
        draw_buffer2 = frame_b;
        buffer_size = frame_bytes;
        LOG_BLE("[DISPLAY] V2 staged flush: 2 PSRAM frames, 2 x %lu-row DMA strips\n",
                static_cast<unsigned long>(kDmaStripRows));
        return true;
    }

    // Previous behaviour: LVGL renders straight into one small DMA buffer.
    if (frame_a) heap_caps_free(frame_a);
    if (frame_b) heap_caps_free(frame_b);
    if (dma_strips[1]) heap_caps_free(dma_strips[1]);
    dma_strips[1] = nullptr;
    staged_flush = false;
    if (!dma_strips[0]) return false;
    draw_buffer = reinterpret_cast<lv_color_t*>(dma_strips[0]);
    draw_buffer2 = nullptr;
    buffer_size = strip_bytes;
    LOG_BLE("[DISPLAY] V2 direct flush fallback (%lu-row buffer)\n",
            static_cast<unsigned long>(kDmaStripRows));
    return true;
}

bool DisplayManager::queue_v2_transfer(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                       const void* pixels) {
    return esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2, y2, pixels) == ESP_OK;
}

bool DisplayManager::wait_for_v2_transfers(uint32_t count) {
    const uint32_t started_ms = millis();
    while (transfers_done.load() < count) {
        if (millis() - started_ms >= kTransferTimeoutMs) {
            LOG_BLE("[DISPLAY] Timed out waiting for panel transfer %lu\n",
                    static_cast<unsigned long>(count));
            return false;
        }
        xSemaphoreTake(transfer_done_signal, pdMS_TO_TICKS(10));
    }
    return true;
}
#endif

void DisplayManager::touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (!g_display_manager) return;
    
    TouchData touch = g_display_manager->touch_driver.get_touch_data();

    if (g_display_manager->filter_touch_for_panel_wake(touch)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    
    if (touch.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch.x;
        data->point.y = touch.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

uint32_t DisplayManager::millis_cb() {
    return millis();
}

void DisplayManager::set_brightness(float brightness) {
#if HW_DISPLAY_VARIANT_V2
    if (!initialized || !panel_io) return;
#else
    if (!initialized || !gfx_device) return;
#endif
    
    // Clamp brightness to valid hardware range [0.0, 1.0]
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    
#if HW_DISPLAY_VARIANT_V2
    if (!startup_frame_presented) {
        deferred_brightness = brightness;
        return;
    }
    apply_brightness(brightness);
#else
    uint8_t brightness_value = (uint8_t)(brightness * 255.0f);
    // Cast to CO5300 and call setBrightness with 8-bit value
    Arduino_CO5300* display = static_cast<Arduino_CO5300*>(gfx_device);
    display->setBrightness(brightness_value);
#endif
}

#if HW_DISPLAY_VARIANT_V2
void DisplayManager::apply_brightness(float brightness) {
    uint8_t brightness_value = (uint8_t)(brightness * 255.0f);
    uint32_t brightness_command = (0x02UL << 24) | (0x51UL << 8);
    esp_lcd_panel_io_tx_param(panel_io, brightness_command,
                              &brightness_value, sizeof(brightness_value));
}

void DisplayManager::present_startup_frame_if_ready() {
    if (startup_frame_presented) return;

    const bool timed_out = millis() - startup_blank_started_ms >= kStartupBlankTimeoutMs;
    // Wait until the last DMA strip of the first frame has been sent.
    const bool frame_complete = startup_frame_rendered && pending_flush_display == nullptr;
    if (!frame_complete && !timed_out) return;

    startup_frame_presented = true;
    apply_brightness(deferred_brightness >= 0.0f ? deferred_brightness
                                                 : USER_SCREEN_BRIGHTNESS_NORMAL);
    LOG_BLE("[DISPLAY] Startup frame presented%s\n", timed_out && !frame_complete ? " (timeout)" : "");
}
#endif

void DisplayManager::set_panel_power(bool powered_on) {
#if HW_DISPLAY_VARIANT_V2
    if (!initialized || !panel_handle) return;
#else
    if (!initialized || !gfx_device) return;
#endif
    if (panel_powered_on == powered_on) return;

#if HW_DISPLAY_VARIANT_V2
    if (esp_lcd_panel_disp_on_off(panel_handle, powered_on) != ESP_OK) {
        LOG_BLE("[DISPLAY] Failed to turn panel %s\n", powered_on ? "on" : "off");
        return;
    }
#else
    if (powered_on) {
        gfx_device->displayOn();
    } else {
        gfx_device->displayOff();
    }
#endif

    panel_powered_on = powered_on;
    if (powered_on && lvgl_display) {
        lv_obj_invalidate(lv_screen_active());
    }
    LOG_BLE("[DISPLAY] Panel %s\n", powered_on ? "on" : "off");
}

bool DisplayManager::filter_touch_for_panel_wake(const TouchData& touch) {
    if (!panel_powered_on) {
        if (touch.just_pressed) {
            set_panel_power(true);
            touch_driver.consume_press_event();
            consume_wake_touch_until_release = true;
            wake_touch_guard_started_ms = millis();
            LOG_BLE("[DISPLAY] Panel wake source: touch\n");
        }
        return true;
    }

    if (!consume_wake_touch_until_release) {
        if (touch.just_pressed) {
            touch_driver.consume_press_event();
        }
        return false;
    }
    if (!touch.pressed) {
        consume_wake_touch_until_release = false;
        wake_touch_guard_started_ms = 0;
        return true;
    }

    // Do not let a stale controller contact suppress every future gesture.
    // One second is comfortably longer than a normal wake tap, while keeping
    // recovery finite if the controller never reports the matching release.
    if (wake_touch_guard_expired(millis() - wake_touch_guard_started_ms)) {
        consume_wake_touch_until_release = false;
        wake_touch_guard_started_ms = 0;
        LOG_BLE("[DISPLAY] Wake-touch guard recovered after missing release\n");
    }
    return true;
}
