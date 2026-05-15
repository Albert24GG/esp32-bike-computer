#pragma once

#include "constants/hw_config.hpp"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "soc/gpio_num.h"

namespace hw {
class SpiBus {
public:
  SpiBus() noexcept = default;

  SpiBus(const SpiBus &) = delete;
  SpiBus &operator=(const SpiBus &) = delete;

  ~SpiBus() noexcept {
    if (initialized_) {
      spi_bus_free(constants::hw::spi::host);
    }
  }

  [[nodiscard]] esp_err_t init() noexcept;

private:
  static constexpr auto tag_ = "SpiBus";

  bool initialized_{false};
};

} // namespace hw