#pragma once

#include "driver/gpio.h"

namespace constants::hw::pins {

constexpr inline gpio_num_t capacitive_pin{GPIO_NUM_1};
constexpr inline gpio_num_t lcd_reset{GPIO_NUM_2};
constexpr inline gpio_num_t lcd_dc{GPIO_NUM_3};
constexpr inline gpio_num_t lcd_cs{GPIO_NUM_4};
constexpr inline gpio_num_t touchscr_cs{GPIO_NUM_5};
constexpr inline gpio_num_t sd_card_cs{GPIO_NUM_6};
constexpr inline gpio_num_t spi_sclk{GPIO_NUM_7};
constexpr inline gpio_num_t spi_miso{GPIO_NUM_8};
constexpr inline gpio_num_t spi_mosi{GPIO_NUM_9};
constexpr inline gpio_num_t lcd_backlight{GPIO_NUM_43};

} // namespace constants::hw::pins