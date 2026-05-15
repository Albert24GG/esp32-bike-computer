#pragma once

#include "driver/touch_sens.h"
#include "driver/touch_version_types.h"
#include "hal/spi_types.h"
#include <cstddef>
#include <string_view>

namespace constants::hw {

namespace touch {

constexpr inline touch_sensor_sample_config_t default_sample_cfg =
    TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5,
                                          TOUCH_VOLT_LIM_H_2V2);
constexpr inline touch_sensor_config_t default_sens_cfg =
    TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(
        1, const_cast<touch_sensor_sample_config_t *>(&default_sample_cfg));
constexpr inline touch_channel_config_t default_chan_cfg = {
    .active_thresh{2000},
    .charge_speed = TOUCH_CHARGE_SPEED_7,
    .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
};

constexpr inline uint32_t channel_id{1};
constexpr inline size_t channel_init_scan_times{3};

// Active threshold to benchmark ratio. (i.e., touch will be activated when data
// >= benchmark * (1 + ratio))
constexpr inline float thresh_ratio{0.02f}; // 2%

} // namespace touch

namespace lcd {
constexpr inline uint32_t hres{240};
constexpr inline uint32_t vres{320};
constexpr inline uint32_t color_depth{16};
constexpr inline uint32_t pclk_hz{40 * 1000 * 1000};
constexpr inline bool mirror_x{false};
constexpr inline bool mirror_y{true};
constexpr inline bool swap_xy{false};
constexpr inline bool invert_color{false};
} // namespace lcd

namespace touchscr {
constexpr inline uint32_t x_max{lcd::hres};
constexpr inline uint32_t y_max{lcd::vres};
constexpr inline bool swap_xy{lcd::swap_xy};
constexpr inline bool mirror_x{lcd::mirror_x};
constexpr inline bool mirror_y{lcd::mirror_y};
constexpr inline uint32_t pclk_hz{2 * 1000 * 1000};
} // namespace touchscr

namespace sdcard {
constexpr inline std::string_view mount_point{"/sdcard"};
constexpr inline uint32_t max_files{5};
constexpr inline bool format_if_mount_failed{false};
} // namespace sdcard

namespace wifi {
constexpr inline uint8_t channel{1};
} // namespace wifi

namespace spi {
constexpr inline spi_host_device_t host{SPI2_HOST};
constexpr inline uint32_t max_transfer_sz{lcd::hres * 80 * lcd::color_depth /
                                          sizeof(std::byte)};
} // namespace spi

} // namespace constants::hw