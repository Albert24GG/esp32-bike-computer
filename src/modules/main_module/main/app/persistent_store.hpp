#pragma once

#include "app_settings.hpp"
#include "esp_err.h"
#include "nvs.h"
#include "ride_metrics.hpp"

#include <cstdint>

namespace app {

struct PersistentRideState {
  uint64_t trip_distance_mm{0};
  uint64_t trip_time_us{0};
  uint64_t total_distance_mm{0};
  uint32_t wheel_boot_id{0};
  uint64_t wheel_cumulative_rotations{0};
  uint64_t wheel_cumulative_ride_time_us{0};
};

class PersistentStore {
public:
  PersistentStore() noexcept = default;
  PersistentStore(const PersistentStore &) = delete;
  PersistentStore &operator=(const PersistentStore &) = delete;

  ~PersistentStore() noexcept;

  [[nodiscard]] esp_err_t init() noexcept;
  [[nodiscard]] esp_err_t load_settings(Settings &settings) noexcept;
  [[nodiscard]] esp_err_t save_settings(const Settings &settings) noexcept;
  [[nodiscard]] esp_err_t load_ride_state(PersistentRideState &state) noexcept;
  [[nodiscard]] esp_err_t save_ride_state(const PersistentRideState &state) noexcept;

private:
  static constexpr auto tag_ = "PersistentStore";
  static constexpr auto namespace_name_ = "bike_app";

  [[nodiscard]] esp_err_t ensure_schema() noexcept;

  nvs_handle_t handle_{0};
  bool initialized_{false};
};

ride_metrics::RideMetrics::InitialState
to_ride_initial_state(const PersistentRideState &state) noexcept;

} // namespace app
