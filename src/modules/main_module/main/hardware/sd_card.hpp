#pragma once

#include <string_view>

#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "constants/hw_config.hpp"

namespace hw {

class SdCard {
public:
  SdCard() noexcept = default;

  SdCard(const SdCard &) = delete;
  SdCard &operator=(const SdCard &) = delete;

  ~SdCard() {
    if (card_ != nullptr) {
      esp_vfs_fat_sdcard_unmount(constants::hw::sdcard::mount_point.data(),
                                 card_);
    }
  }

  [[nodiscard]] esp_err_t mount() noexcept;

private:
  static constexpr auto tag_ = "SdCard";

  sdmmc_card_t *card_{nullptr};
};
} // namespace hw