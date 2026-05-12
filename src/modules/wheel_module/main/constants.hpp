#pragma once

#include "esp_now.h"
#include "soc/gpio_num.h"
#include <array>
#include <cstdint>
#include <cstring>

constexpr inline size_t WHEEL_PERIODS_BUF_SIZE = 10;
constexpr inline gpio_num_t SENSOR_PIN = GPIO_NUM_0;
constexpr inline uint32_t WAKEUP_CNT_BEFORE_RECALIBRATION = 60;
constexpr inline uint32_t SLOW_CLK_CAL_CYCLES = 1024;
constexpr inline uint32_t LP_WAKEUPS_BEFORE_TIMEOUT = 60;
constexpr inline uint32_t LP_TIMER_DURATION_US = 1'000'000; // 1 second
constexpr inline auto TAG = "wheel_module";

constexpr inline auto DEST_MAC_ADDR =
    std::array<uint8_t, 6>{{0xe8, 0xf6, 0xa, 0x8d, 0x28, 0xb0}};
constexpr inline auto ESPNOW_CHANNEL = 1;

constexpr inline auto PEER_INFO = []() {
  esp_now_peer_info_t peer_info = {};
  for (size_t i = 0; i < 6; ++i) {
    peer_info.peer_addr[i] = DEST_MAC_ADDR[i];
  }
  peer_info.channel = ESPNOW_CHANNEL;
  peer_info.ifidx = WIFI_IF_STA;
  peer_info.encrypt = false;
  return peer_info;
}();