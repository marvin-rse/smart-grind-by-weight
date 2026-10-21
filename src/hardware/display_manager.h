#pragma once
#include "../config/hardware.h"
#if HW_DISPLAY_VARIANT_V2
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#else
#include <Arduino_GFX_Library.h>
#endif
#include <lvgl.h>
#include <atomic>
#include "touch_driver.h"
#include "../config/constants.h"

struct DisplayPerformanceSnapshot {
    uint32_t window_ms = 0;
    uint32_t ui_calls = 0;
    uint32_t refreshes = 0;
    uint32_t rendered_frames = 0;
    uint32_t flushes = 0;
    uint32_t pixels = 0;
    uint32_t ui_time_us = 0;
    uint32_t render_time_us = 0;
    uint32_t flush_time_us = 0;
};

class DisplayManager {
private:
#if HW_DISPLAY_VARIANT_V2
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_panel_handle_t panel_handle;
    lv_display_t* pending_flush_display;
    // The SH8601 GRAM holds random data after power-up. Keep the backlight at
    // zero until LVGL has pushed its first complete frame, then apply the
    // brightness requested in the meantime.
    bool startup_frame_rendered = false;
    bool startup_frame_presented = false;
    float deferred_brightness = -1.0f;
    uint32_t startup_blank_started_ms = 0;
    // Staged flush: LVGL renders whole frames into PSRAM and the flush copies
    // them strip by strip into two small internal DMA buffers in turn, so one
    // strip transfers while the next is copied. Falls back to rendering
    // straight into a single DMA buffer when memory is short.
    bool staged_flush = false;
    uint16_t* dma_strips[2] = {nullptr, nullptr};
    uint32_t dma_strip_rows = 0;
    lv_color_t* draw_buffer2 = nullptr;
    std::atomic<uint32_t> transfers_expected{0};
    std::atomic<uint32_t> transfers_done{0};
    SemaphoreHandle_t transfer_done_signal = nullptr;
#else
    Arduino_DataBus* bus;
    Arduino_GFX* gfx_device;
#endif
    lv_display_t* lvgl_display;
    lv_indev_t* lvgl_input;
    lv_color_t* draw_buffer;
#if !HW_DISPLAY_VARIANT_V2
    uint16_t* dma_staging_buffer;
#endif
    TouchDriver touch_driver;
#if !HW_DISPLAY_VARIANT_V2
    uint16_t dma_staging_rows;
#endif
    
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t buffer_size;
    bool initialized;
    bool panel_powered_on;
    bool consume_wake_touch_until_release;
    uint32_t wake_touch_guard_started_ms;

    portMUX_TYPE metrics_mux = portMUX_INITIALIZER_UNLOCKED;
    DisplayPerformanceSnapshot metrics_window;
    DisplayPerformanceSnapshot metrics_snapshot;
    uint32_t metrics_window_started_ms = 0;
    uint32_t render_started_us = 0;

public:
    void init();
    void update();
    void set_brightness(float brightness);
    void set_panel_power(bool powered_on);
    bool is_panel_powered_on() const { return panel_powered_on; }
    bool draw_rgb565_file(const char* path, uint16_t width, uint16_t height);
    
    uint32_t get_width() const { return screen_width; }
    uint32_t get_height() const { return screen_height; }
    bool is_initialized() const { return initialized; }
    TouchDriver* get_touch_driver() { return &touch_driver; }
    DisplayPerformanceSnapshot get_performance_snapshot();
    
private:
    static void display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
#if HW_DISPLAY_VARIANT_V2
    static bool color_transfer_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                       esp_lcd_panel_io_event_data_t* event_data,
                                       void* user_context);
#endif
    static void display_rounder_cb(lv_event_t* e);
    static void display_metrics_cb(lv_event_t* e);
    static void touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    bool filter_touch_for_panel_wake(const TouchData& touch);
    static uint32_t millis_cb();
#if HW_DISPLAY_VARIANT_V2
    bool allocate_v2_buffers();
    bool queue_v2_transfer(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const void* pixels);
    bool wait_for_v2_transfers(uint32_t count);
    void apply_brightness(float brightness);
    void present_startup_frame_if_ready();
#endif
};

extern DisplayManager* g_display_manager;
