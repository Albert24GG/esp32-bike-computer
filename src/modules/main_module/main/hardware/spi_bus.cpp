#include "spi_bus.hpp"
#include "esp_check.h"
#include "esp_err.h"

namespace hw {

esp_err_t SpiBus::init() noexcept {
  spi_bus_config_t bus_cfg = {};

  bus_cfg.mosi_io_num = cfg_.mosi;
  bus_cfg.miso_io_num = cfg_.miso;
  bus_cfg.sclk_io_num = cfg_.sclk;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz = cfg_.max_transfer_sz;

  ESP_RETURN_ON_ERROR(spi_bus_initialize(cfg_.host, &bus_cfg, SPI_DMA_CH_AUTO),
                      tag_, "spi_bus_initialize failed");

  initialized_ = true;
  return ESP_OK;
}

} // namespace hw