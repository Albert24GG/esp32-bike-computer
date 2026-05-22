#include "ride_metrics.hpp"

namespace app::ride_metrics {

void RideMetrics::register_speed_period(uint64_t period_us) noexcept {
  if (period_us < constants::app::ride_metrics::min_valid_period_us ||
      period_us > constants::app::ride_metrics::max_valid_period_us) {
    return;
  }

  const float new_speed_kmph =
      static_cast<float>(wheel_circumference_mm_) * 3600.0f /
      static_cast<float>(period_us);

  if (!is_speed_initialized_) {
    current_speed_kmph_ = new_speed_kmph;
    is_speed_initialized_ = true;
  } else {
    // Exponential moving average to smooth the speed readings
    current_speed_kmph_ =
        constants::app::ride_metrics::speed_smoothing_factor * new_speed_kmph +
        (1 - constants::app::ride_metrics::speed_smoothing_factor) *
            current_speed_kmph_;
  }
}

void RideMetrics::register_wheel_update(uint64_t rotation_delta,
                                        uint64_t ride_time_delta_us,
                                        const uint64_t *recent_periods_us,
                                        size_t recent_periods_len) noexcept {
  const uint64_t distance_delta_mm =
      rotation_delta * static_cast<uint64_t>(wheel_circumference_mm_);

  trip_distance_mm_ += distance_delta_mm;
  total_distance_mm_ += distance_delta_mm;
  trip_time_us_ += ride_time_delta_us;

  for (size_t i = 0; i < recent_periods_len; ++i) {
    register_speed_period(recent_periods_us[i]);
  }
}

void RideMetrics::reset_trip() noexcept {
  trip_distance_mm_ = 0;
  trip_time_us_ = 0;
  reset_speed();
}

RideMetrics::Snapshot RideMetrics::snapshot() const noexcept {
  const float average_speed_kmph =
      trip_time_us_ == 0
          ? 0.0f
          : static_cast<float>(trip_distance_mm_) * 3600.0f /
                static_cast<float>(trip_time_us_);

  return {
      .trip_distance_mm = trip_distance_mm_,
      .trip_time_us = trip_time_us_,
      .total_distance_mm = total_distance_mm_,
      .current_speed_kmph = current_speed_kmph_,
      .average_speed_kmph = average_speed_kmph,
      .moving = is_speed_initialized_,
  };
}

} // namespace app::ride_metrics
