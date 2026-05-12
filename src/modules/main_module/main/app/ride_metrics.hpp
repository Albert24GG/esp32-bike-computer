#pragma once

#include <cstdint>

#include "constants/app_config.hpp"

namespace app::ride_metrics {
class RideMetrics {
public:
  struct InitialState {
    uint64_t initial_distance_m{0};
    uint64_t initial_time_s{0};
  };

  explicit RideMetrics(const InitialState &initial_state) noexcept
      : total_distance_m_(initial_state.initial_distance_m),
        total_time_s_(initial_state.initial_time_s) {}

  void set_wheel_circumference(float circumference_m) noexcept {
    wheel_circumference_m_ = circumference_m;
  }

  void register_wheel_rotation(uint64_t period_us) noexcept;

  void reset_speed() noexcept { is_speed_initialized_ = false; }

  float get_current_speed_kmph() const noexcept { return current_speed_kmph_; }

private:
  float current_speed_kmph_{0.0f};
  bool is_speed_initialized_{false};
  uint64_t total_distance_m_{0};
  uint64_t total_time_s_{0};
  float wheel_circumference_m_{
      constants::app::ride_metrics::default_wheel_circumference_m};
};
} // namespace app::ride_metrics