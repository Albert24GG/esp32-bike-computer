#pragma once

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

#include <cstdint>
#include <cstdio>
#include <utility>

namespace utils {

template <size_t N, typename F> void repeat_static(F &&fun) {
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    ((void(I), fun()), ...);
  }(std::make_index_sequence<N>{});
}

esp_err_t set_idle_high(gpio_num_t pin);

} // namespace utils