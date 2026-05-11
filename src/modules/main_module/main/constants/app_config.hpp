#pragma once

#include "portmacro.h"
#include <chrono>

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

} // namespace constants::app