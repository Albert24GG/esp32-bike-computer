#include "lvgl_port_wrapper.hpp"
#include "esp_check.h"

#include "constants/hw_config.hpp"

namespace app::graphics {

esp_err_t LvglPort::init() noexcept {
  const lvgl_port_cfg_t cfg = ESP_LVGL_PORT_INIT_CONFIG();

  ESP_RETURN_ON_ERROR(lvgl_port_init(&cfg), tag_, "lvgl_port_init failed");

  initialized_ = true;
  return ESP_OK;
}

esp_err_t LvglPort::add_display(hw::Ili9341Display &lcd) noexcept {
  using namespace constants::hw;

  lvgl_port_display_cfg_t disp_cfg = {};
  disp_cfg.io_handle = lcd.io();
  disp_cfg.panel_handle = lcd.panel();
  disp_cfg.buffer_size = lcd::hres * 40;
  disp_cfg.double_buffer = true;
  disp_cfg.hres = lcd::hres;
  disp_cfg.vres = lcd::vres;
  disp_cfg.monochrome = false;
  disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;

  disp_cfg.rotation.swap_xy = lcd::swap_xy;
  disp_cfg.rotation.mirror_x = lcd::mirror_x;
  disp_cfg.rotation.mirror_y = lcd::mirror_y;

  disp_cfg.flags.buff_dma = true;
  disp_cfg.flags.swap_bytes = true;

  display_ = lvgl_port_add_disp(&disp_cfg);
  ESP_RETURN_ON_FALSE(display_ != nullptr, ESP_FAIL, tag_,
                      "lvgl_port_add_disp failed");

  return ESP_OK;
}

esp_err_t LvglPort::add_touch(hw::Xpt2046Touch &touch) noexcept {
  ESP_RETURN_ON_ERROR(touch.add_to_lvgl(display_), tag_,
                      "Failed to add touch to lvgl");

  return ESP_OK;
}

} // namespace app::graphics