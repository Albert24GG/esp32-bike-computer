#pragma once

#include "portmacro.h"
#include <chrono>
#include <numbers>
#include <cstdint>

namespace constants::app::tasks::main {
constexpr inline int32_t queue_size{10};
constexpr inline auto task_name{"main_event_loop"};
constexpr inline UBaseType_t task_priority{10};
constexpr inline uint32_t task_stack_size{6144};
constexpr inline BaseType_t task_core_id{APP_CPU_NUM};
} // namespace constants::app::tasks::main

namespace constants::app::tasks::ui {
constexpr inline auto task_name{"ui_update"};
constexpr inline UBaseType_t task_priority{4};
constexpr inline uint32_t task_stack_size{4096};
constexpr inline BaseType_t task_core_id{APP_CPU_NUM};
constexpr inline auto update_period = std::chrono::milliseconds(500);
} // namespace constants::app::tasks::ui

namespace constants::app {
constexpr inline auto log_tag{"main_module"};

namespace settings {
constexpr inline uint16_t default_wheel_circumference_mm{2100};
constexpr inline uint8_t default_brightness_percent{80};
constexpr inline uint16_t default_sleep_timeout_s{30};
constexpr inline uint16_t min_wheel_circumference_mm{1000};
constexpr inline uint16_t max_wheel_circumference_mm{2500};
constexpr inline uint8_t min_brightness_percent{0};
constexpr inline uint8_t max_brightness_percent{100};
constexpr inline uint16_t sleep_timeout_options_s[] = {30,  60,  120,
                                                       180, 240, 300};
} // namespace settings

namespace persistence {
constexpr inline uint32_t schema_version{1};
constexpr inline auto ride_save_interval = std::chrono::minutes(5);
constexpr inline uint64_t ride_save_interval_us =
    std::chrono::duration_cast<std::chrono::microseconds>(ride_save_interval)
        .count();
constexpr inline uint64_t ride_save_distance_threshold_mm{500'000};
} // namespace persistence

namespace ride_metrics {
// Smoothing factor used for exponential moving average calculation of speed
constexpr inline float speed_smoothing_factor{0.2f};
constexpr inline auto speed_inactivity_timeout = std::chrono::seconds(5);
constexpr inline auto speed_inactivity_timeout_us =
    std::chrono::duration_cast<std::chrono::microseconds>(
        speed_inactivity_timeout)
        .count();
constexpr inline uint64_t min_valid_period_us{50'000};
constexpr inline uint64_t max_valid_period_us{30'000'000};
} // namespace ride_metrics

namespace maps {
constexpr inline uint8_t fixed_zoom{16};
constexpr inline uint16_t tile_size_px{256};
constexpr inline const char *tile_root{"/sdcard/maps"};
constexpr inline const char *tile_extension{".rgb565"};
constexpr inline uint8_t min_rerender_delta_px{2};
} // namespace maps

} // namespace constants::app
