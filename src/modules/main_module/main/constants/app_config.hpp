#pragma once

#include "portmacro.h"
#include <chrono>
#include <numbers>

namespace constants::app::tasks::main {
constexpr inline int32_t queue_size{10};
constexpr inline auto task_name{"main_event_loop"};
constexpr inline UBaseType_t task_priority{10};
constexpr inline uint32_t task_stack_size{4096};
constexpr inline BaseType_t task_core_id{APP_CPU_NUM};
} // namespace constants::app::tasks::main

namespace constants::app {
constexpr inline auto log_tag{"main_module"};
constexpr inline auto timeout_period = std::chrono::seconds(30);
constexpr inline auto timeout_period_us =
    std::chrono::duration_cast<std::chrono::microseconds>(timeout_period)
        .count();

namespace ride_metrics {
// Smoothing factor used for exponential moving average calculation of speed
constexpr inline float speed_smoothing_factor{0.2f};
// Use 27.5 inches as the default wheel diameter
constexpr inline float inch_to_meter{0.0254f};
constexpr inline float default_wheel_circumference_m{27.5f * inch_to_meter *
                                                     std::numbers::pi};
constexpr inline auto speed_inactivity_timeout = std::chrono::seconds(5);
constexpr inline auto speed_inactivity_timeout_us =
    std::chrono::duration_cast<std::chrono::microseconds>(
        speed_inactivity_timeout)
        .count();
} // namespace ride_metrics

} // namespace constants::app