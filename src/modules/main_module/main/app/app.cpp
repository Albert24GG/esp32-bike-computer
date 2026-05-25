#include "app.hpp"

#include "../../../common/espnow_packet.hpp"
#include "constants/app_config.hpp"
#include "ui_ScreenMaps.h"
#include "ui_presenter.hpp"
#include "utils.hpp"

#include "driver/touch_sens.h"
#include "driver/touch_sens_types.h"
#include "driver/touch_version_types.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_log_level.h"
#include "esp_now.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "rom/ets_sys.h"
#include "ui.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
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

ESP_EVENT_DEFINE_BASE(APP_EVENTS);
enum {
  APP_EVENT_SLEEP_TIMEOUT,
  APP_EVENT_ESPNOW_RECV,
  APP_EVENT_PERSISTENCE_SAVE,
  APP_EVENT_BLE_LOCATION,
  APP_EVENT_BLE_STATE
};

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

    const touch_event_callbacks_t callbacks = [] {
      touch_event_callbacks_t callbacks{};
      callbacks.on_active = [](touch_sensor_handle_t sens_handle,
                               const touch_active_event_data_t *event,
                               void *user_ctx) -> bool {
        App &self = *static_cast<App *>(user_ctx);
        ESP_EARLY_LOGI(log_tag,
                       "Touch channel %d activated, resetting timeout timer...",
                       event->chan_id);
        self.reset_sleep_timeout();
        return false;
      };
      return callbacks;
    }();

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

esp_err_t App::init() noexcept {
  if (initialized_) {
    ESP_LOGW(log_tag, "App already initialized");
    return ESP_OK;
  }

  ESP_LOGI(log_tag, "Initializing app...");

  ESP_RETURN_ON_ERROR(init_main_event_loop(), log_tag,
                      "Failed to initialize main event loop");
  ESP_RETURN_ON_ERROR(init_storage(), log_tag, "Failed to initialize storage");
  ESP_RETURN_ON_ERROR(init_timeout_timer(), log_tag,
                      "Failed to initialize timeout timer");
  ESP_RETURN_ON_ERROR(init_persistence_timer(), log_tag,
                      "Failed to initialize persistence timer");
  ESP_RETURN_ON_ERROR(init_touch_wakeup(), log_tag,
                      "Failed to initialize touch wakeup source");
  ESP_RETURN_ON_ERROR(init_wifi(), log_tag, "Failed to initialize WiFi");
  ESP_RETURN_ON_ERROR(init_espnow(), log_tag, "Failed to initialize ESP-NOW");
  ESP_RETURN_ON_ERROR(init_ble_location(), log_tag,
                      "Failed to initialize BLE location service");
  ESP_RETURN_ON_ERROR(init_speed_inactivity_timer(), log_tag,
                      "Failed to initialize speed inactivity timer");

  const esp_err_t hardware_err = init_hardware();
  if (hardware_err == ESP_OK) {
    ESP_RETURN_ON_ERROR(init_ui(), log_tag, "Failed to initialize UI");
    ESP_RETURN_ON_ERROR(init_ui_update_task(), log_tag,
                        "Failed to initialize UI update task");
    ui_ready_ = true;
  } else {
    ESP_LOGE(log_tag,
             "Hardware/UI initialization failed (%s); continuing headless so "
             "BLE and ESP-NOW logs remain available",
             esp_err_to_name(hardware_err));
  }

  initialized_ = true;

  ESP_LOGI(log_tag, "App initialized successfully");

  return ESP_OK;
}

esp_err_t App::init_storage() noexcept {
  ESP_RETURN_ON_ERROR(persistent_store_.init(), log_tag,
                      "Failed to initialize persistent store");

  Settings loaded_settings{};
  ESP_RETURN_ON_ERROR(persistent_store_.load_settings(loaded_settings), log_tag,
                      "Failed to load settings");
  settings_ = normalize_settings(loaded_settings);

  ESP_RETURN_ON_ERROR(persistent_store_.load_ride_state(loaded_ride_state_),
                      log_tag, "Failed to load ride state");
  ride_metrics_.restore(to_ride_initial_state(loaded_ride_state_));
  ride_metrics_.set_wheel_circumference_mm(settings_.wheel_circumference_mm);
  last_wheel_boot_id_ = loaded_ride_state_.wheel_boot_id;
  last_wheel_cumulative_rotations_ =
      loaded_ride_state_.wheel_cumulative_rotations;
  last_wheel_cumulative_ride_time_us_ =
      loaded_ride_state_.wheel_cumulative_ride_time_us;

  return ESP_OK;
}

esp_err_t App::init_timeout_timer() noexcept {
  const esp_timer_create_args_t timer_args = [this] {
    esp_timer_create_args_t timer_args{};

    timer_args.callback = App::sleep_timeout_timer_cb;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "main_timeout_timer";

    return timer_args;
  }();

  ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &timeout_timer_handle_),
                      log_tag, "Failed to create timeout timer");
  reset_sleep_timeout();

  return ESP_OK;
}

esp_err_t App::init_persistence_timer() noexcept {
  esp_timer_create_args_t timer_args{};
  timer_args.callback = App::persistence_timer_cb;
  timer_args.arg = this;
  timer_args.dispatch_method = ESP_TIMER_TASK;
  timer_args.name = "persistence_timer";

  ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &persistence_timer_handle_),
                      log_tag, "Failed to create persistence timer");
  ESP_RETURN_ON_ERROR(
      esp_timer_start_periodic(
          persistence_timer_handle_,
          constants::app::persistence::ride_save_interval_us),
      log_tag, "Failed to start persistence timer");

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

  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register_with(
          main_event_loop_handle_, APP_EVENTS, APP_EVENT_SLEEP_TIMEOUT,
          App::sleep_timeout_handler, nullptr, nullptr),
      log_tag, "Failed to register timeout event handler for main event loop");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register_with(
          main_event_loop_handle_, APP_EVENTS, APP_EVENT_ESPNOW_RECV,
          App::espnow_recv_handler, nullptr, nullptr),
      log_tag,
      "Failed to register ESP-NOW receive event handler for main event loop");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register_with(
          main_event_loop_handle_, APP_EVENTS, APP_EVENT_PERSISTENCE_SAVE,
          App::persistence_save_handler, nullptr, nullptr),
      log_tag,
      "Failed to register persistence event handler for main event loop");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register_with(
          main_event_loop_handle_, APP_EVENTS, APP_EVENT_BLE_LOCATION,
          App::ble_location_handler, nullptr, nullptr),
      log_tag,
      "Failed to register BLE location event handler for main event loop");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_instance_register_with(
          main_event_loop_handle_, APP_EVENTS, APP_EVENT_BLE_STATE,
          App::ble_state_handler, nullptr, nullptr),
      log_tag,
      "Failed to register BLE state event handler for main event loop");

  return ESP_OK;
}

esp_err_t App::init_wifi() noexcept {
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
  ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_MIN_MODEM), log_tag,
                      "esp_wifi_set_ps failed");
  ESP_LOGI(log_tag, "WiFi power save set to minimum modem sleep");

  return ESP_OK;
}

esp_err_t App::init_espnow() noexcept {
  ESP_RETURN_ON_ERROR(esp_now_init(), log_tag, "esp_now_init failed");

  ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(App::espnow_recv_cb), log_tag,
                      "esp_now_register_recv_cb failed");

  return ESP_OK;
}

esp_err_t App::init_ble_location() noexcept {
  BleLocationService::Config config{};
  config.location_callback = App::ble_location_cb;
  config.location_callback_ctx = this;
  config.state_callback = App::ble_state_cb;
  config.state_callback_ctx = this;

  ESP_RETURN_ON_ERROR(ble_location_.init(config), log_tag,
                      "BLE location init failed");

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

esp_err_t App::init_hardware() noexcept {
  using namespace constants::hw;
  using namespace hw;

  ESP_RETURN_ON_ERROR(hardware_.spi_bus.init(), log_tag, "SPI bus init failed");

  // Important before SD init when sharing the SPI bus.
  ESP_RETURN_ON_ERROR(utils::set_idle_high(pins::lcd_cs), log_tag,
                      "LCD CS idle-high failed");
  ESP_RETURN_ON_ERROR(utils::set_idle_high(pins::touchscr_cs), log_tag,
                      "touch CS idle-high failed");

  const esp_err_t sd_err = hardware_.sd.mount();
  if (sd_err != ESP_OK) {
    ESP_LOGW(log_tag, "SD mount failed; maps will use the fallback background: %s",
             esp_err_to_name(sd_err));
  }

  ESP_RETURN_ON_ERROR(hardware_.lcd.init(), log_tag, "LCD init failed");

  ESP_RETURN_ON_ERROR(lvgl_.init(), log_tag, "LVGL port init failed");
  ESP_RETURN_ON_ERROR(lvgl_.add_display(hardware_.lcd), log_tag,
                      "LVGL display add failed");

  ESP_RETURN_ON_ERROR(hardware_.touch.init(), log_tag, "touch init failed");
  ESP_RETURN_ON_ERROR(lvgl_.add_touch(hardware_.touch), log_tag,
                      "LVGL touch add failed");
  if (hardware_.touch.indev() != nullptr) {
    lv_indev_add_event_cb(hardware_.touch.indev(), App::lvgl_input_event_cb,
                          LV_EVENT_PRESSED, this);
  }

  ESP_RETURN_ON_ERROR(
      hardware_.lcd.set_backlight_brightness(settings_.brightness_percent),
      log_tag, "backlight brightness setup failed");

  return ESP_OK;
}

esp_err_t App::init_ui() noexcept {
  lvgl_.lock();
  ui_init();
  ui_presenter::write_settings(settings_);
  refresh_ui_unlocked();
  lvgl_.unlock();

  return ESP_OK;
}

esp_err_t App::init_ui_update_task() noexcept {
  using namespace constants::app::tasks::ui;

  const BaseType_t result = xTaskCreatePinnedToCore(
      App::ui_update_task, task_name, task_stack_size, this, task_priority,
      &ui_update_task_handle_, task_core_id);
  ESP_RETURN_ON_FALSE(result == pdPASS, ESP_FAIL, log_tag,
                      "Failed to create UI update task");

  return ESP_OK;
}

void App::reset_sleep_timeout() noexcept {
  if (timeout_timer_handle_ == nullptr) {
    return;
  }

  uint16_t sleep_timeout_s{};
  {
    std::lock_guard lock(state_mutex_);
    sleep_timeout_s = settings_.sleep_timeout_s;
  }

  const esp_err_t stop_err = esp_timer_stop(timeout_timer_handle_);
  if (stop_err != ESP_OK && stop_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(log_tag, "Failed to stop sleep timeout timer: %s",
             esp_err_to_name(stop_err));
  }

  const uint64_t timeout_us =
      static_cast<uint64_t>(sleep_timeout_s) * 1'000'000ULL;
  const esp_err_t start_err =
      esp_timer_start_once(timeout_timer_handle_, timeout_us);
  if (start_err != ESP_OK) {
    ESP_LOGW(log_tag, "Failed to start sleep timeout timer: %s",
             esp_err_to_name(start_err));
  }
}

PersistentRideState App::persistent_ride_state_unlocked() const noexcept {
  const auto snapshot = ride_metrics_.snapshot();
  return {
      .trip_distance_mm = snapshot.trip_distance_mm,
      .trip_time_us = snapshot.trip_time_us,
      .total_distance_mm = snapshot.total_distance_mm,
      .wheel_boot_id = last_wheel_boot_id_,
      .wheel_cumulative_rotations = last_wheel_cumulative_rotations_,
      .wheel_cumulative_ride_time_us = last_wheel_cumulative_ride_time_us_,
  };
}

void App::save_ride_state_if_needed(bool force) noexcept {
  PersistentRideState state{};
  uint64_t saved_distance_budget = 0;
  uint64_t saved_time_budget = 0;

  {
    std::lock_guard lock(state_mutex_);
    if (!ride_state_dirty_) {
      return;
    }

    if (!force &&
        unsaved_distance_mm_ <
            constants::app::persistence::ride_save_distance_threshold_mm &&
        unsaved_ride_time_us_ <
            constants::app::persistence::ride_save_interval_us) {
      return;
    }

    state = persistent_ride_state_unlocked();
    saved_distance_budget = unsaved_distance_mm_;
    saved_time_budget = unsaved_ride_time_us_;
  }

  const esp_err_t err = persistent_store_.save_ride_state(state);
  if (err != ESP_OK) {
    ESP_LOGE(log_tag, "Failed to save ride state: %s", esp_err_to_name(err));
    return;
  }

  {
    std::lock_guard lock(state_mutex_);
    unsaved_distance_mm_ =
        unsaved_distance_mm_ > saved_distance_budget
            ? unsaved_distance_mm_ - saved_distance_budget
            : 0;
    unsaved_ride_time_us_ =
        unsaved_ride_time_us_ > saved_time_budget
            ? unsaved_ride_time_us_ - saved_time_budget
            : 0;
    ride_state_dirty_ = unsaved_distance_mm_ > 0 || unsaved_ride_time_us_ > 0;
  }
}

void App::apply_settings(const Settings &settings) noexcept {
  const Settings normalized = normalize_settings(settings);
  Settings previous{};

  {
    std::lock_guard lock(state_mutex_);
    previous = settings_;
    settings_ = normalized;
    ride_metrics_.set_wheel_circumference_mm(
        normalized.wheel_circumference_mm);
  }

  if (previous.brightness_percent != normalized.brightness_percent) {
    if (ui_ready_) {
      const esp_err_t err =
          hardware_.lcd.set_backlight_brightness(normalized.brightness_percent);
      if (err != ESP_OK) {
        ESP_LOGE(log_tag, "Failed to apply brightness: %s",
                 esp_err_to_name(err));
      }
    }
  }

  if (previous.sleep_timeout_s != normalized.sleep_timeout_s) {
    reset_sleep_timeout();
  }

  if (previous != normalized) {
    const esp_err_t err = persistent_store_.save_settings(normalized);
    if (err != ESP_OK) {
      ESP_LOGE(log_tag, "Failed to save settings: %s", esp_err_to_name(err));
    }
  }
}

void App::refresh_ui_unlocked() noexcept {
  ride_metrics::RideMetrics::Snapshot snapshot{};
  Settings settings{};

  {
    std::lock_guard lock(state_mutex_);
    snapshot = ride_metrics_.snapshot();
    settings = settings_;
  }

  ui_presenter::write_metrics(snapshot, settings);
}

void App::refresh_ui_from_task() noexcept {
  if (!ui_ready_) {
    return;
  }

  lvgl_.lock();
  refresh_ui_unlocked();
  lvgl_.unlock();
}

void App::on_reset_trip() noexcept {
  {
    std::lock_guard lock(state_mutex_);
    ride_metrics_.reset_trip();
    ride_state_dirty_ = true;
  }

  save_ride_state_if_needed(true);
  if (ui_ready_) {
    refresh_ui_unlocked();
  }
  reset_sleep_timeout();
}

void App::on_exit_screen_settings() noexcept {
  Settings fallback{};
  {
    std::lock_guard lock(state_mutex_);
    fallback = settings_;
  }

  apply_settings(ui_presenter::read_settings(fallback));
  Settings updated{};
  {
    std::lock_guard lock(state_mutex_);
    updated = settings_;
  }
  ui_presenter::write_settings(updated);
  refresh_ui_unlocked();
  reset_sleep_timeout();
}

void App::on_enter_screen_maps() noexcept {
  ble_location_.set_location_updates_requested(true);
  reset_sleep_timeout();
}

void App::on_exit_screen_maps() noexcept {
  ble_location_.set_location_updates_requested(false);
  const esp_err_t err = ble_location_.cancel_pairing();
  if (err != ESP_OK) {
    ESP_LOGW(log_tag, "Failed to stop BLE pairing after leaving maps: %s",
             esp_err_to_name(err));
  }
  reset_sleep_timeout();
}

void App::on_ble_start_pairing() noexcept {
  const esp_err_t err = ble_location_.start_pairing();
  if (err != ESP_OK) {
    ESP_LOGE(log_tag, "Failed to start BLE pairing: %s", esp_err_to_name(err));
  } else if (ui_ready_) {
    lvgl_.lock();
    ui_presenter::write_maps_connecting();
    lvgl_.unlock();
  }
  reset_sleep_timeout();
}

void App::on_ble_cancel_pairing() noexcept {
  const esp_err_t err = ble_location_.cancel_pairing();
  if (err != ESP_OK) {
    ESP_LOGE(log_tag, "Failed to cancel BLE pairing: %s", esp_err_to_name(err));
  } else if (ui_ready_) {
    lvgl_.lock();
    ui_presenter::write_maps_not_connected();
    lvgl_.unlock();
  }
  reset_sleep_timeout();
}

void App::speed_inactivity_timer_cb(void *arg) noexcept {
  App &self = *static_cast<App *>(arg);
  std::lock_guard lock(self.state_mutex_);
  self.ride_metrics_.reset_speed();
}

void App::espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                         const uint8_t *data, int data_len) noexcept {
  if (data_len != sizeof(BikePacket)) {
    ESP_LOGW(log_tag, "Received ESP-NOW packet with invalid length: %d",
             data_len);
    return;
  }

  App &app = App::get_instance();

  ESP_ERROR_CHECK(esp_event_post_to(app.main_event_loop_handle_, APP_EVENTS,
                                    APP_EVENT_ESPNOW_RECV, (void *)data,
                                    data_len, portMAX_DELAY));
}

void App::espnow_recv_handler(void *event_handler_arg,
                              esp_event_base_t event_base, int32_t event_id,
                              void *event_data) noexcept {

  App &app = App::get_instance();

  BikePacket packet{};
  memcpy(&packet, event_data, sizeof(BikePacket));

  ESP_LOGI(log_tag,
           "Received ESP-NOW packet: boot_id=%lu, seq_num=%llu, "
           "sample_count=%u",
           static_cast<unsigned long>(packet.boot_id), packet.seq_num,
           packet.periods_buf_len);

  if (packet.periods_buf_len > max_periods_per_packet) {
    ESP_LOGW(log_tag, "Received ESP-NOW packet with invalid sample count: %u",
             packet.periods_buf_len);
    return;
  }

  {
    std::lock_guard lock(app.state_mutex_);

    const bool new_wheel_boot = packet.boot_id != app.last_wheel_boot_id_;

    if (!new_wheel_boot && packet.seq_num <= app.last_packet_seq_num_) {
      ESP_LOGW(log_tag,
               "Received out-of-order or duplicate packet: boot_id=%lu, "
               "seq_num=%llu, last_seq_num=%llu",
               static_cast<unsigned long>(packet.boot_id), packet.seq_num,
               app.last_packet_seq_num_);
      return;
    }

    if (new_wheel_boot) {
      ESP_LOGI(log_tag,
               "Wheel boot id changed: previous=%lu, current=%lu; accepting "
               "new wheel session",
               static_cast<unsigned long>(app.last_wheel_boot_id_),
               static_cast<unsigned long>(packet.boot_id));
    }

    app.last_wheel_boot_id_ = packet.boot_id;
    app.last_packet_seq_num_ = packet.seq_num;

    const bool wheel_counter_reset =
        new_wheel_boot ||
        packet.cumulative_rotations < app.last_wheel_cumulative_rotations_ ||
        packet.cumulative_ride_time_us <
            app.last_wheel_cumulative_ride_time_us_;

    const uint64_t rotation_delta =
        wheel_counter_reset
            ? packet.cumulative_rotations
            : packet.cumulative_rotations -
                  app.last_wheel_cumulative_rotations_;
    const uint64_t ride_time_delta_us =
        wheel_counter_reset
            ? packet.cumulative_ride_time_us
            : packet.cumulative_ride_time_us -
                  app.last_wheel_cumulative_ride_time_us_;

    if (wheel_counter_reset) {
      ESP_LOGW(log_tag,
               "Wheel cumulative counters reset; using packet counters as new "
               "delta baseline");
    }

    uint64_t recent_periods_us[max_periods_per_packet]{};
    memcpy(recent_periods_us, packet.periods_buf_us,
           packet.periods_buf_len * sizeof(recent_periods_us[0]));

    app.ride_metrics_.register_wheel_update(
        rotation_delta, ride_time_delta_us, recent_periods_us,
        packet.periods_buf_len);
    app.last_wheel_cumulative_rotations_ = packet.cumulative_rotations;
    app.last_wheel_cumulative_ride_time_us_ =
        packet.cumulative_ride_time_us;
    app.unsaved_distance_mm_ +=
        rotation_delta *
        static_cast<uint64_t>(app.settings_.wheel_circumference_mm);
    app.unsaved_ride_time_us_ += ride_time_delta_us;
    app.ride_state_dirty_ =
        app.ride_state_dirty_ || rotation_delta > 0 || ride_time_delta_us > 0;
  }

  app.save_ride_state_if_needed(false);
  app.reset_sleep_timeout();

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

void App::sleep_timeout_timer_cb(void *arg) noexcept {
  App &self = *static_cast<App *>(arg);

  ESP_ERROR_CHECK(esp_event_post_to(self.main_event_loop_handle_, APP_EVENTS,
                                    APP_EVENT_SLEEP_TIMEOUT, nullptr, 0,
                                    portMAX_DELAY));
}

void App::persistence_timer_cb(void *arg) noexcept {
  App &self = *static_cast<App *>(arg);

  const esp_err_t err =
      esp_event_post_to(self.main_event_loop_handle_, APP_EVENTS,
                        APP_EVENT_PERSISTENCE_SAVE, nullptr, 0, 0);
  if (err != ESP_OK) {
    ESP_EARLY_LOGW(log_tag, "Failed to post persistence save event: %s",
                   esp_err_to_name(err));
  }
}

void App::ui_update_task(void *arg) noexcept {
  App &self = *static_cast<App *>(arg);

  while (true) {
    self.refresh_ui_from_task();
    vTaskDelay(pdMS_TO_TICKS(
        constants::app::tasks::ui::update_period.count()));
  }
}

void App::lvgl_input_event_cb(lv_event_t *event) noexcept {
  App &self = *static_cast<App *>(lv_event_get_user_data(event));
  self.reset_sleep_timeout();
}

void App::ble_location_cb(const LocationCoordinates &location,
                          void *ctx) noexcept {
  App &self = *static_cast<App *>(ctx);
  const esp_err_t err =
      esp_event_post_to(self.main_event_loop_handle_, APP_EVENTS,
                        APP_EVENT_BLE_LOCATION, &location, sizeof(location), 0);
  if (err != ESP_OK) {
    ESP_EARLY_LOGW(log_tag, "Failed to post BLE location event: %s",
                   esp_err_to_name(err));
  }
}

void App::ble_state_cb(BleLocationState state, void *ctx) noexcept {
  App &self = *static_cast<App *>(ctx);
  const esp_err_t err =
      esp_event_post_to(self.main_event_loop_handle_, APP_EVENTS,
                        APP_EVENT_BLE_STATE, &state, sizeof(state), 0);
  if (err != ESP_OK) {
    ESP_EARLY_LOGW(log_tag, "Failed to post BLE state event: %s",
                   esp_err_to_name(err));
  }
}

void App::sleep_timeout_handler(void *event_handler_arg,
                                esp_event_base_t event_base, int32_t event_id,
                                void *event_data) noexcept {
  ESP_LOGI(log_tag, "Received sleep timeout event in main event "
                    "loop, entering deep sleep...");

  App::get_instance().save_ride_state_if_needed(true);

  ESP_ERROR_CHECK(uart_wait_tx_idle_polling(
      static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM)));

  esp_deep_sleep_start();
}

void App::persistence_save_handler(void *event_handler_arg,
                                   esp_event_base_t event_base,
                                   int32_t event_id,
                                   void *event_data) noexcept {
  App::get_instance().save_ride_state_if_needed(false);
}

void App::ble_location_handler(void *event_handler_arg,
                               esp_event_base_t event_base, int32_t event_id,
                               void *event_data) noexcept {
  App &app = App::get_instance();

  if (app.lvgl_.active_screen() != ui_ScreenMaps) {
    ESP_LOGI(log_tag,
             "Received BLE location event but maps screen is not active, "
             "ignoring location update");
    return;
  }

  const auto &location = *static_cast<LocationCoordinates *>(event_data);

  {
    std::lock_guard lock(app.state_mutex_);
    app.last_location_ = location;
    app.has_location_ = true;
  }

  ESP_LOGI(log_tag,
           "BLE location accepted: lat=%.6f lon=%.6f accuracy=%.1fm "
           "timestamp_ms=%llu",
           location.latitude, location.longitude,
           static_cast<double>(location.accuracy_m), location.timestamp_ms);

  if (app.ui_ready_) {
    app.lvgl_.lock();
    ui_presenter::write_maps_location(location.latitude, location.longitude,
                                      location.accuracy_m);
    app.map_renderer_.render_location(location);
    app.lvgl_.unlock();
  }

  app.reset_sleep_timeout();
}

void App::ble_state_handler(void *event_handler_arg,
                            esp_event_base_t event_base, int32_t event_id,
                            void *event_data) noexcept {
  App &app = App::get_instance();
  const auto state = *static_cast<BleLocationState *>(event_data);

  ESP_LOGI(log_tag, "BLE location state changed: %u",
           static_cast<unsigned>(state));

  if (app.ui_ready_) {
    app.lvgl_.lock();
    switch (state) {
    case BleLocationState::Idle:
      ui_presenter::write_maps_not_connected();
      break;
    case BleLocationState::Advertising:
      ui_presenter::write_maps_connecting();
      break;
    case BleLocationState::Connected: {
      LocationCoordinates location{};
      bool has_location{};
      {
        std::lock_guard lock(app.state_mutex_);
        location = app.last_location_;
        has_location = app.has_location_;
      }
      if (has_location) {
        ui_presenter::write_maps_location(location.latitude, location.longitude,
                                          location.accuracy_m);
        app.map_renderer_.render_location(location);
      } else {
        ui_presenter::write_maps_connected_waiting();
      }
      break;
    }
    }
    app.lvgl_.unlock();
  }

  app.reset_sleep_timeout();
}

} // namespace app
