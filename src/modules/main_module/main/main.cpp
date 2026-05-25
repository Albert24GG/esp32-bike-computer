#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "rom/uart.h"

#include "app/app.hpp"
#include "constants/app_config.hpp"

extern "C" void app_main() {
  app::App &app = app::App::get_instance();

  ESP_ERROR_CHECK(app.init());

  const auto wakeup_causes = esp_sleep_get_wakeup_causes();
  if (wakeup_causes & ESP_SLEEP_WAKEUP_TOUCHPAD) {
    ESP_LOGI(constants::app::log_tag, "Woke up from touchpad!");
    uart_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
  }

  vTaskSuspend(nullptr);
}