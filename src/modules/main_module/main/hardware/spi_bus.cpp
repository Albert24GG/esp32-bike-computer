#include "spi_bus.hpp"
#include "esp_check.h"
#include "esp_err.h"

#include "constants/board_pins.hpp"

namespace hw {

esp_err_t SpiBus::init() noexcept {
  using namespace constants::hw;

  spi_bus_config_t bus_cfg = {};

  bus_cfg.mosi_io_num = pins::spi_mosi;
  bus_cfg.miso_io_num = pins::spi_miso;
  bus_cfg.sclk_io_num = pins::spi_sclk;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz = spi::max_transfer_sz;

  ESP_RETURN_ON_ERROR(spi_bus_initialize(spi::host, &bus_cfg, SPI_DMA_CH_AUTO),
                      tag_, "spi_bus_initialize failed");

  initialized_ = true;
  return ESP_OK;
}

} // namespace hw