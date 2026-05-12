#include "ride_metrics.hpp"

namespace app::ride_metrics {

void RideMetrics::register_wheel_rotation(uint64_t period_us) noexcept {
  float new_speed_kmph =
      1e6 * wheel_circumference_m_ / period_us * 3.6f; // Convert m/s to km/h

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

  // Update total distance and time
  total_distance_m_ += wheel_circumference_m_;
  total_time_s_ += period_us / 1e6f;
}

} // namespace app::ride_metrics