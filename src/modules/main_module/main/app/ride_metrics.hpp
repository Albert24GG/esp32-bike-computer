#pragma once

#include <cstddef>
#include <cstdint>

#include "constants/app_config.hpp"

namespace app::ride_metrics {
class RideMetrics {
public:
  struct InitialState {
    uint64_t trip_distance_mm{0};
    uint64_t trip_time_us{0};
    uint64_t total_distance_mm{0};
  };

  struct Snapshot {
    uint64_t trip_distance_mm{0};
    uint64_t trip_time_us{0};
    uint64_t total_distance_mm{0};
    float current_speed_kmph{0.0f};
    float average_speed_kmph{0.0f};
    bool moving{false};
  };

  explicit RideMetrics(const InitialState &initial_state) noexcept
      : trip_distance_mm_(initial_state.trip_distance_mm),
        trip_time_us_(initial_state.trip_time_us),
        total_distance_mm_(initial_state.total_distance_mm) {}

  void restore(const InitialState &initial_state) noexcept {
    trip_distance_mm_ = initial_state.trip_distance_mm;
    trip_time_us_ = initial_state.trip_time_us;
    total_distance_mm_ = initial_state.total_distance_mm;
    reset_speed();
  }

  void set_wheel_circumference_mm(uint16_t circumference_mm) noexcept {
    wheel_circumference_mm_ = circumference_mm;
  }

  void register_wheel_update(uint64_t rotation_delta,
                             uint64_t ride_time_delta_us,
                             const uint64_t *recent_periods_us,
                             size_t recent_periods_len) noexcept;

  void reset_trip() noexcept;

  void reset_speed() noexcept {
    current_speed_kmph_ = 0.0f;
    is_speed_initialized_ = false;
  }

  [[nodiscard]] Snapshot snapshot() const noexcept;

private:
  void register_speed_period(uint64_t period_us) noexcept;

  float current_speed_kmph_{0.0f};
  bool is_speed_initialized_{false};
  uint64_t trip_distance_mm_{0};
  uint64_t trip_time_us_{0};
  uint64_t total_distance_mm_{0};
  uint16_t wheel_circumference_mm_{
      constants::app::settings::default_wheel_circumference_mm};
};
} // namespace app::ride_metrics
