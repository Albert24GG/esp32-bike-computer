#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_timer.h"

namespace app {

ESP_EVENT_DECLARE_BASE(APP_EVENTS);
enum { APP_EVENT_TIMEOUT };

class App {
public:
  [[nodiscard]] esp_err_t init() noexcept;

  [[nodiscard]] esp_err_t signal_timeout() noexcept;

private:
  [[nodiscard]] esp_err_t init_touch_wakeup() noexcept;
  [[nodiscard]] esp_err_t init_main_event_loop() noexcept;
  [[nodiscard]] esp_err_t init_timeout_timer() noexcept;

  esp_event_loop_handle_t main_event_loop_handle_;
  esp_timer_handle_t timeout_timer_handle_;
  bool initialized_{false};
};

} // namespace app