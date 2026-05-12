#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "ride_metrics.hpp"

namespace app {

ESP_EVENT_DECLARE_BASE(APP_EVENTS);
enum { APP_EVENT_TIMEOUT };

class App {
public:
  static App& get_instance() noexcept {
    static App instance;
    return instance;
  }

  [[nodiscard]] esp_err_t init() noexcept;

  [[nodiscard]] esp_err_t signal_timeout() noexcept;


  // TEST ONLY
  float get_current_speed_kmph() const noexcept { return ride_metrics_.get_current_speed_kmph(); }

private:
  App() = default;

  [[nodiscard]] esp_err_t init_touch_wakeup() noexcept;
  [[nodiscard]] esp_err_t init_main_event_loop() noexcept;
  [[nodiscard]] esp_err_t init_timeout_timer() noexcept;
  [[nodiscard]] esp_err_t init_wifi() noexcept;
  [[nodiscard]] esp_err_t init_espnow() noexcept;
  [[nodiscard]] esp_err_t init_speed_inactivity_timer() noexcept;

  static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data,
                             int data_len);
  static void speed_inactivity_timer_cb(void *arg);

  esp_event_loop_handle_t main_event_loop_handle_{};
  esp_timer_handle_t timeout_timer_handle_{};
  esp_timer_handle_t speed_inactivity_timer_handle_{};
  bool initialized_{false};
  ride_metrics::RideMetrics ride_metrics_{{}};
  uint64_t last_packet_seq_num_{0};
};

} // namespace app