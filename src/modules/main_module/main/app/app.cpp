#include "app.hpp"

#include "constants/app_config.hpp"
#include "constants/hw_config.hpp"
#include "esp_now.h"
#include "esp_wifi.h"
#include "rom/ets_sys.h"

#include "../../../common/espnow_packet.hpp"
#include "driver/touch_sens.h"
#include "driver/touch_sens_types.h"
#include "driver/touch_version_types.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_log_level.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include <cstdint>
#include <ranges>

constexpr auto &log_tag = constants::app::log_tag;

namespace {

esp_err_t touch_initial_calibration(touch_sensor_handle_t sens_handle,
                                    touch_channel_handle_t chan_handle) {
  /* Enable the touch sensor to do the initial scanning, so that to initialize
   * the channel data */
  ESP_RETURN_ON_ERROR(touch_sensor_enable(sens_handle), log_tag,
                      "Failed to enable touch sensor");

  /* Scan the enabled touch channel for several times, to make sure the initial
   * channel data is stable */

  for (auto _ :
       std::views::repeat(0, constants::hw::touch::channel_init_scan_times)) {
    ESP_RETURN_ON_ERROR(
        touch_sensor_trigger_oneshot_scanning(sens_handle, 2000), log_tag,
        "Failed to trigger oneshot scanning");
  }

  /* Disable the touch channel to rollback the state */
  ESP_RETURN_ON_ERROR(touch_sensor_disable(sens_handle), log_tag,
                      "Failed to disable touch sensor");

  /* Read the initial channel benchmark and reconfig the channel active
   * threshold accordingly */

  {
    uint32_t benchmark{};

    ESP_RETURN_ON_ERROR(touch_channel_read_data(chan_handle,
                                                TOUCH_CHAN_DATA_TYPE_BENCHMARK,
                                                &benchmark),
                        log_tag, "Failed to read touch channel benchmark");
    touch_channel_config_t chan_cfg = constants::hw::touch::default_chan_cfg;
    chan_cfg.active_thresh[0] = benchmark * constants::hw::touch::thresh_ratio;

    ESP_RETURN_ON_ERROR(touch_sensor_reconfig_channel(chan_handle, &chan_cfg),
                        log_tag, "Failed to reconfig touch channel");

    ESP_LOGD(log_tag, "Touch channel benchmark: %d, active threshold: %d",
             benchmark, chan_cfg.active_thresh[0]);
  }

  return ESP_OK;
}
} // namespace

namespace app {

esp_err_t App::init_touch_wakeup() noexcept {
  /* Handles of touch sensor */
  touch_sensor_handle_t sens_handle{};
  touch_channel_handle_t chan_handle{};

  {
    /* Step 1: Create a new touch sensor controller handle with default sample
     * configuration */
    constexpr touch_sensor_config_t sens_cfg =
        constants::hw::touch::default_sens_cfg;
    ESP_RETURN_ON_ERROR(touch_sensor_new_controller(&sens_cfg, &sens_handle),
                        log_tag, "Failed to create touch sensor controller");
  }

  {
    /* Step 2: Create and enable the new touch channel handles with default
     * configurations */
    constexpr touch_channel_config_t chan_cfg =
        constants::hw::touch::default_chan_cfg;
    ESP_RETURN_ON_ERROR(
        touch_sensor_new_channel(sens_handle, constants::hw::touch::channel_id,
                                 &chan_cfg, &chan_handle),
        log_tag, "Failed to create touch channel");

    /* Display the touch channel corresponding GPIO number, you can also
    know from `touch_sensor_channel.h` */
    touch_chan_info_t chan_info{};
    ESP_RETURN_ON_ERROR(touch_sensor_get_channel_info(chan_handle, &chan_info),
                        log_tag, "Failed to get touch channel info");
    ESP_LOGD(log_tag, "Touch channel enabled on GPIO%d", chan_info.chan_gpio);
  }

  {
    /* Step 3: Configure the default filter for the touch sensor */
    constexpr touch_sensor_filter_config_t filter_cfg =
        TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_RETURN_ON_ERROR(touch_sensor_config_filter(sens_handle, &filter_cfg),
                        log_tag, "Failed to configure touch sensor filter");
  }

  /* Do the initial scanning to initialize the touch channel data
   * Without this step, the channel data in the first read will be invalid
   */
  ESP_RETURN_ON_ERROR(touch_initial_calibration(sens_handle, chan_handle),
                      log_tag,
                      "Failed to do the initial calibration for touch channel");

  {
    /* Register callback for resetting the timeout timer */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    touch_event_callbacks_t callbacks = {
        .on_active = [](touch_sensor_handle_t sens_handle,
                        const touch_active_event_data_t *event,
                        void *user_ctx) -> bool {
          const App &self = *static_cast<App *>(user_ctx);
          ESP_EARLY_LOGI(
              log_tag, "Touch channel %d activated, resetting timeout timer...",
              event->chan_id);
          ESP_ERROR_CHECK(esp_timer_stop(self.timeout_timer_handle_));
          ESP_ERROR_CHECK(esp_timer_start_once(
              self.timeout_timer_handle_, constants::app::timeout_period_us));
          return false;
        }};
#pragma GCC diagnostic pop

    ESP_RETURN_ON_ERROR(
        touch_sensor_register_callbacks(sens_handle, &callbacks, this), log_tag,
        "Failed to register touch sensor event callbacks");
  }

  {
    /* Step 4: Enable the deep sleep wake-up with the basic configuration */
    /* Get the channel information to use same active threshold for the sleep
     * channel */
    touch_chan_info_t chan_info = {};
    ESP_RETURN_ON_ERROR(touch_sensor_get_channel_info(chan_handle, &chan_info),
                        log_tag, "Failed to get touch channel info");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    const touch_sleep_config_t slp_cfg = TOUCH_SENSOR_DEFAULT_DSLP_PD_CONFIG(
        chan_handle, chan_info.active_thresh[0]);
#pragma GCC diagnostic pop

    ESP_LOGD(log_tag,
             "Touch sleep wake-up configured with channel %d (GPIO%d), deep "
             "sleep allow power down: %d",
             chan_info.chan_id, chan_info.chan_gpio, slp_cfg.deep_slp_allow_pd);
    ESP_RETURN_ON_ERROR(touch_sensor_config_sleep_wakeup(sens_handle, &slp_cfg),
                        log_tag,
                        "Failed to configure touch sleep wake-up function");
  }

  /* Step 5: Enable touch sensor controller and start continuous scanning before
   * entering light sleep */
  ESP_RETURN_ON_ERROR(touch_sensor_enable(sens_handle), log_tag,
                      "Failed to enable touch sensor");
  ESP_RETURN_ON_ERROR(touch_sensor_start_continuous_scanning(sens_handle),
                      log_tag,
                      "Failed to start touch sensor continuous scanning");

  ESP_LOGI(log_tag, "touch wakeup source is ready");

  return ESP_OK;
}

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

esp_err_t App::signal_timeout() noexcept {
  ESP_LOGI(log_tag, "timeout!");

  ESP_RETURN_ON_ERROR(esp_event_post_to(main_event_loop_handle_, APP_EVENTS,
                                        APP_EVENT_TIMEOUT, nullptr, 0,
                                        portMAX_DELAY),
                      log_tag, "Failed to post timeout event");

  return ESP_OK;
}

esp_err_t App::init() noexcept {

  ESP_LOGI(log_tag, "Initializing app...");
  if (initialized_) {
    ESP_LOGW(log_tag, "App already initialized");
    return ESP_OK;
  }

  ESP_LOGI(log_tag, "Initializing main event loop...");

  ESP_RETURN_ON_ERROR(init_main_event_loop(), log_tag,
                      "Failed to initialize main event loop");
  ESP_LOGI(log_tag, "Initializing touch wakeup source...");

  ESP_RETURN_ON_ERROR(init_touch_wakeup(), log_tag,
                      "Failed to initialize touch wakeup source");
  ESP_LOGI(log_tag, "Initializing WiFi and ESP-NOW...");
  ESP_RETURN_ON_ERROR(init_wifi(), log_tag, "Failed to initialize WiFi");
  ESP_LOGI(log_tag, "Initializing ESP-NOW...");
  ESP_RETURN_ON_ERROR(init_espnow(), log_tag, "Failed to initialize ESP-NOW");
  ESP_LOGI(log_tag, "Initializing timeout timer...");
  ESP_RETURN_ON_ERROR(init_timeout_timer(), log_tag,
                      "Failed to initialize timeout timer");
  ESP_LOGI(log_tag, "Initializing speed inactivity timer...");
  ESP_RETURN_ON_ERROR(init_speed_inactivity_timer(), log_tag,
                      "Failed to initialize speed inactivity timer");

  ESP_LOGI(log_tag, "App initialization complete");
  initialized_ = true;
  ESP_LOGI(log_tag,
           "initialized=%d, timeout_timer_handle=%p, "
           "speed_inactivity_timer_handle=%p",
           initialized_, timeout_timer_handle_, speed_inactivity_timer_handle_);
  ESP_LOGI(log_tag, "core = %d, app_ptr = %p", xPortGetCoreID(),
           &App::get_instance());
  return ESP_OK;
}

esp_err_t App::init_timeout_timer() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
  const esp_timer_create_args_t timer_args = {
      .callback =
          [](void *arg) {
            App &self = *static_cast<App *>(arg);
            ESP_ERROR_CHECK(self.signal_timeout());
          },
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "main_timeout_timer",
  };
#pragma GCC diagnostic pop

  ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timeout_timer_handle_),
                      log_tag, "Failed to create timeout timer");
  ESP_RETURN_ON_ERROR(esp_timer_start_once(timeout_timer_handle_,
                                           constants::app::timeout_period_us),
                      log_tag, "Failed to start timeout timer");

  return ESP_OK;
}

esp_err_t App::init_main_event_loop() noexcept {

  using namespace constants::app::tasks::main;

  constexpr esp_event_loop_args_t loop_args = {.queue_size = queue_size,
                                               .task_name = task_name,
                                               .task_priority = task_priority,
                                               .task_stack_size =
                                                   task_stack_size,
                                               .task_core_id = task_core_id};

  ESP_RETURN_ON_ERROR(
      esp_event_loop_create(&loop_args, &main_event_loop_handle_), log_tag,
      "Failed to create main event loop");

  // TODO: Handle more events through this event handler instead of running the
  // code directly in the callbacks. The callbacks should only post events to
  // this event loop
  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register_with(
          main_event_loop_handle_, APP_EVENTS, APP_EVENT_TIMEOUT,
          [](void *arg, esp_event_base_t event_base, int32_t event_id,
             void *event_data) {
            ESP_LOGI(log_tag, "Received timeout event in main event "
                              "loop, entering deep sleep...");
            ESP_LOGI(constants::app::log_tag, "Entering deep sleep...");

            std::fflush(stdout);
            ESP_ERROR_CHECK(uart_wait_tx_idle_polling(
                static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM)));

            esp_deep_sleep_start();
          },
          nullptr, nullptr),
      log_tag, "Failed to register timeout event handler for main event loop");

  return ESP_OK;
}

esp_err_t App::init_wifi() noexcept {
  {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_RETURN_ON_ERROR(nvs_flash_erase(), log_tag, "nvs erase failed");
      ESP_RETURN_ON_ERROR(nvs_flash_init(), log_tag, "nvs init failed");
    } else {
      ESP_RETURN_ON_ERROR(ret, log_tag, "nvs init failed");
    }
  }

  ESP_RETURN_ON_ERROR(esp_netif_init(), log_tag, "esp_netif_init failed");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), log_tag,
                      "event loop create failed");

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();

  ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), log_tag,
                      "esp_wifi_init failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), log_tag,
                      "esp_wifi_set_storage failed");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), log_tag,
                      "esp_wifi_set_mode failed");
  ESP_RETURN_ON_ERROR(esp_wifi_start(), log_tag, "esp_wifi_start failed");

  ESP_RETURN_ON_ERROR(
      esp_wifi_set_channel(constants::hw::wifi::channel, WIFI_SECOND_CHAN_NONE),
      log_tag, "esp_wifi_set_channel failed");

  return ESP_OK;
}

esp_err_t App::init_espnow() noexcept {
  ESP_RETURN_ON_ERROR(esp_now_init(), log_tag, "esp_now_init failed");

  ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(App::espnow_recv_cb), log_tag,
                      "esp_now_register_recv_cb failed");

  return ESP_OK;
}

esp_err_t App::init_speed_inactivity_timer() noexcept {
  esp_timer_create_args_t timer_args{};
  timer_args.callback = App::speed_inactivity_timer_cb;
  timer_args.arg = this;
  timer_args.name = "speed_inactivity_timer";

  ESP_RETURN_ON_ERROR(
      esp_timer_create(&timer_args, &speed_inactivity_timer_handle_), log_tag,
      "Failed to create speed inactivity timer");
  ESP_RETURN_ON_ERROR(
      esp_timer_start_once(
          speed_inactivity_timer_handle_,
          constants::app::ride_metrics::speed_inactivity_timeout_us),
      log_tag, "Failed to start speed inactivity timer");

  return ESP_OK;
}

void App::speed_inactivity_timer_cb(void *arg) {
  App &self = *static_cast<App *>(arg);
  self.ride_metrics_.reset_speed();
}

void App::espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                         const uint8_t *data, int data_len) {
  if (data_len != sizeof(BikePacket)) {
    ESP_LOGW(log_tag, "Received ESP-NOW packet with invalid length: %d",
             data_len);
    return;
  }

  App &app = App::get_instance();

  BikePacket packet{};
  memcpy(&packet, data, sizeof(BikePacket));
  ESP_LOGI(
      log_tag,
      "Received ESP-NOW packet from " MACSTR ": seq_num=%u, sample_count=%u",
      MAC2STR(recv_info->src_addr), packet.seq_num, packet.periods_buf_len);

  if (packet.seq_num <= app.last_packet_seq_num_) {
    ESP_LOGW(log_tag,
             "Received out-of-order or duplicate packet: seq_num=%u, "
             "last_seq_num=%u",
             packet.seq_num, app.last_packet_seq_num_);
    return;
  }

  app.last_packet_seq_num_ = packet.seq_num;

  for (uint8_t i : std::views::iota(uint8_t{0}, packet.periods_buf_len)) {
    const uint64_t period_us = packet.periods_buf_us[i];
    app.ride_metrics_.register_wheel_rotation(period_us);
  }

  // Reset the speed inactivity timer
  esp_err_t err = esp_timer_stop(app.speed_inactivity_timer_handle_);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE &&
      err != ESP_ERR_INVALID_ARG) {
    ESP_ERROR_CHECK(err);
  }
  ESP_ERROR_CHECK(esp_timer_start_once(
      app.speed_inactivity_timer_handle_,
      constants::app::ride_metrics::speed_inactivity_timeout_us));
}

} // namespace app