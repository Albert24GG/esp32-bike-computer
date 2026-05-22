#pragma once

#include "constants/app_config.hpp"

#include <algorithm>
#include <cstdint>

namespace app {

enum class UnitSystem : uint8_t { Metric = 0, Imperial = 1 };

struct Settings {
  UnitSystem unit_system{UnitSystem::Metric};
  uint16_t wheel_circumference_mm{
      constants::app::settings::default_wheel_circumference_mm};
  uint8_t brightness_percent{
      constants::app::settings::default_brightness_percent};
  uint16_t sleep_timeout_s{constants::app::settings::default_sleep_timeout_s};

  bool operator==(const Settings &other) const noexcept = default;
};

inline uint16_t clamp_wheel_circumference(uint16_t value) noexcept {
  return std::clamp(value, constants::app::settings::min_wheel_circumference_mm,
                    constants::app::settings::max_wheel_circumference_mm);
}

inline uint8_t clamp_brightness(uint8_t value) noexcept {
  return std::clamp(value, constants::app::settings::min_brightness_percent,
                    constants::app::settings::max_brightness_percent);
}

inline bool is_valid_sleep_timeout(uint16_t value) noexcept {
  for (const uint16_t option :
       constants::app::settings::sleep_timeout_options_s) {
    if (option == value) {
      return true;
    }
  }

  return false;
}

inline uint16_t normalize_sleep_timeout(uint16_t value) noexcept {
  return is_valid_sleep_timeout(value)
             ? value
             : constants::app::settings::default_sleep_timeout_s;
}

inline Settings normalize_settings(Settings settings) noexcept {
  if (settings.unit_system != UnitSystem::Metric &&
      settings.unit_system != UnitSystem::Imperial) {
    settings.unit_system = UnitSystem::Metric;
  }

  settings.wheel_circumference_mm =
      clamp_wheel_circumference(settings.wheel_circumference_mm);
  settings.brightness_percent = clamp_brightness(settings.brightness_percent);
  settings.sleep_timeout_s = normalize_sleep_timeout(settings.sleep_timeout_s);
  return settings;
}

} // namespace app
