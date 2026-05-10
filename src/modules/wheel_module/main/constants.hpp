#pragma once

#include "soc/gpio_num.h"
#include <cstdint>

constexpr inline size_t WHEEL_PERIODS_BUF_SIZE = 10;
constexpr inline gpio_num_t SENSOR_PIN = GPIO_NUM_0;
constexpr inline uint32_t WAKEUP_CNT_BEFORE_RECALIBRATION = 60;
constexpr inline uint32_t SLOW_CLK_CAL_CYCLES = 1024;
constexpr inline uint32_t LP_WAKEUPS_BEFORE_TIMEOUT = 60;
constexpr inline uint32_t LP_TIMER_DURATION_US = 1'000'000; // 1 second