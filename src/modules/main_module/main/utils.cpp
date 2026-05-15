#include "utils.hpp"

#include "driver/gpio.h"

namespace utils {

esp_err_t set_idle_high(gpio_num_t pin) {
  if (pin == GPIO_NUM_NC) {
    return ESP_OK;
  }

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;

  if (auto err = gpio_config(&cfg); err != ESP_OK) {
    return err;
  }
  if (auto err = gpio_set_level(pin, 1); err != ESP_OK) {
    return err;
  }

  return ESP_OK;
}
} // namespace utils