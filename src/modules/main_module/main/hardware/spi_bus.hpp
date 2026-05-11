#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"
#include "soc/gpio_num.h"

namespace hw {
class SpiBus {
public:
  struct Config {
    spi_host_device_t host{};
    gpio_num_t mosi{};
    gpio_num_t miso{};
    gpio_num_t sclk{};
    int max_transfer_sz{};
  };

  explicit SpiBus(Config cfg) noexcept : cfg_{cfg} {}

  SpiBus(const SpiBus &) = delete;
  SpiBus &operator=(const SpiBus &) = delete;

  ~SpiBus() noexcept {
    if (initialized_) {
      spi_bus_free(cfg_.host);
    }
  }

  [[nodiscard]] esp_err_t init() noexcept;

  [[nodiscard]] spi_host_device_t host() const { return cfg_.host; }

private:
  static constexpr auto tag_ = "SpiBus";

  const Config cfg_{};
  bool initialized_{false};
};

} // namespace hw