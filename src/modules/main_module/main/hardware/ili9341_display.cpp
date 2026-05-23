#include "ili9341_display.hpp"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_io_spi.h"

#include "constants/board_pins.hpp"
#include "constants/hw_config.hpp"

namespace hw {

esp_err_t Ili9341Display::init() noexcept {
  using namespace constants::hw;

  if (initialized_) {
    ESP_LOGW(tag_, "Display already initialized");
    return ESP_OK;
  }

  if (pins::lcd_backlight != GPIO_NUM_NC) {
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = backlight::speed_mode;
    timer_cfg.duty_resolution = backlight::duty_resolution;
    timer_cfg.timer_num = backlight::timer;
    timer_cfg.freq_hz = backlight::frequency_hz;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), tag_,
                        "backlight ledc timer config failed");

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = pins::lcd_backlight;
    channel_cfg.speed_mode = backlight::speed_mode;
    channel_cfg.channel = backlight::channel;
    channel_cfg.timer_sel = backlight::timer;
    channel_cfg.duty = 0;
    channel_cfg.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_cfg), tag_,
                        "backlight ledc channel config failed");
    backlight_pwm_initialized_ = true;
  }

  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = pins::lcd_cs;
  io_cfg.dc_gpio_num = pins::lcd_dc;
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = lcd::pclk_hz;
  io_cfg.trans_queue_depth = 10;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;

  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(spi::host),
                               &io_cfg, &io_),
      tag_, "esp_lcd_new_panel_io_spi failed");

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = pins::lcd_reset;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panel_cfg.bits_per_pixel = 16;

  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io_, &panel_cfg, &panel_), tag_,
                      "esp_lcd_new_panel_ili9341 failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_), tag_, "panel reset failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_), tag_, "panel init failed");

  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_mirror(panel_, lcd::mirror_x, lcd::mirror_y), tag_,
      "panel mirror failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_, lcd::swap_xy), tag_,
                      "panel swap_xy failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_, lcd::invert_color),
                      tag_, "panel invert failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_, true), tag_,
                      "panel display on failed");

  if (pins::lcd_backlight != GPIO_NUM_NC) {
    ESP_RETURN_ON_ERROR(set_backlight_brightness(100), tag_,
                        "backlight initial brightness failed");
  }

  initialized_ = true;
  return ESP_OK;
}

esp_err_t Ili9341Display::set_backlight_brightness(uint8_t percent) noexcept {
  using namespace constants::hw;

  if (pins::lcd_backlight == GPIO_NUM_NC || !backlight_pwm_initialized_) {
    return ESP_OK;
  }

  const uint32_t clamped_percent = percent > 100 ? 100 : percent;
  // Simple curve for better low-end control
  const uint32_t curved_percent = (clamped_percent * clamped_percent) / 100;

  const uint32_t duty =
      backlight::min_safe_duty +
      (backlight::max_duty - backlight::min_safe_duty) * curved_percent / 100;

  ESP_RETURN_ON_ERROR(
      ledc_set_duty(backlight::speed_mode, backlight::channel, duty), tag_,
      "backlight ledc_set_duty failed");
  ESP_RETURN_ON_ERROR(
      ledc_update_duty(backlight::speed_mode, backlight::channel), tag_,
      "backlight ledc_update_duty failed");

  return ESP_OK;
}
} // namespace hw
