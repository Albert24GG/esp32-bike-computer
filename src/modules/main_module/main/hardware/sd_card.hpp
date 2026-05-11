#pragma once

#include <string_view>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

namespace hw {

class SdCard {
public:
    struct Config {
        spi_host_device_t host{};
        gpio_num_t cs{};
        std::string_view mount_point{"/sdcard"};
        int max_files{5};
        bool format_if_mount_failed{false};
    };

    explicit SdCard(Config cfg) : cfg_{cfg} {}

    SdCard(const SdCard&) = delete;
    SdCard& operator=(const SdCard&) = delete;

    ~SdCard() {
        if (card_ != nullptr) {
            esp_vfs_fat_sdcard_unmount(cfg_.mount_point.data(), card_);
        }
    }

    [[nodiscard]] esp_err_t mount() noexcept;

    [[nodiscard]] const char* mount_point() const {
        return cfg_.mount_point.data();
    }

private:
    static constexpr auto tag_ = "SdCard";

    const Config cfg_{};
    sdmmc_card_t* card_{nullptr};
};
}