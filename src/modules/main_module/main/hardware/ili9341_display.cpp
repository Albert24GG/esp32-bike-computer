#include "ili9341_display.hpp"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_io_spi.h"

namespace hw {

esp_err_t Ili9341Display::init() noexcept {
  if (initialized_) {
    ESP_LOGW(tag_, "Display already initialized");
    return ESP_OK;
  }

  if (cfg_.backlight != GPIO_NUM_NC) {
    gpio_config_t bl_cfg = {};
    bl_cfg.pin_bit_mask = 1ULL << cfg_.backlight;
    bl_cfg.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), tag_,
                        "backlight gpio_config failed");
    gpio_set_level(cfg_.backlight, 0);
  }

  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = cfg_.cs;
  io_cfg.dc_gpio_num = cfg_.dc;
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = cfg_.pclk_hz;
  io_cfg.trans_queue_depth = 10;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;

  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(cfg_.host),
                               &io_cfg, &io_),
      tag_, "esp_lcd_new_panel_io_spi failed");

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = cfg_.rst;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;

  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io_, &panel_cfg, &panel_), tag_,
                      "esp_lcd_new_panel_ili9341 failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_), tag_, "panel reset failed");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_), tag_, "panel init failed");

  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_mirror(panel_, cfg_.mirror_x, cfg_.mirror_y), tag_,
      "panel mirror failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_, cfg_.swap_xy), tag_,
                      "panel swap_xy failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_, cfg_.invert_color),
                      tag_, "panel invert failed");

  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_, true), tag_,
                      "panel display on failed");

  if (cfg_.backlight != GPIO_NUM_NC) {
    gpio_set_level(cfg_.backlight, 1);
  }

  initialized_ = true;
  return ESP_OK;
}
} // namespace hw