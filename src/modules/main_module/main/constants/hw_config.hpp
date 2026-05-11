#pragma once

#include "driver/touch_sens.h"
#include "driver/touch_version_types.h"
#include "hal/spi_types.h"

namespace constants::hw::touch {

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

constexpr inline int channel_id{1};
constexpr inline size_t channel_init_scan_times{3};

// Active threshold to benchmark ratio. (i.e., touch will be activated when data
// >= benchmark * (1 + ratio))
constexpr inline float thresh_ratio{0.02f}; // 2%

} // namespace constants::hw::touch

namespace constants::hw::lcd {
    constexpr inline spi_host_device_t spi_host{SPI2_HOST};
    constexpr inline int hres{240};
    constexpr inline int vres{320};
}