#include "sd_card.hpp"
#include "constants/hw_config.hpp"
#include "esp_check.h"
#include "esp_vfs_fat.h"

#include "constants/board_pins.hpp"

namespace hw {

esp_err_t SdCard::mount() noexcept {
  using namespace constants::hw;

  esp_vfs_fat_sdmmc_mount_config_t mount_cfg = VFS_FAT_MOUNT_DEFAULT_CONFIG();
  mount_cfg.format_if_mount_failed = sdcard::format_if_mount_failed;
  mount_cfg.max_files = sdcard::max_files;
  mount_cfg.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = spi::host;

  sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_cfg.gpio_cs = pins::sd_card_cs;
  slot_cfg.host_id = spi::host;

  ESP_RETURN_ON_ERROR(esp_vfs_fat_sdspi_mount(sdcard::mount_point.data(), &host,
                                              &slot_cfg, &mount_cfg, &card_),
                      tag_, "esp_vfs_fat_sdspi_mount failed");

  sdmmc_card_print_info(stdout, card_);
  return ESP_OK;
}

} // namespace hw