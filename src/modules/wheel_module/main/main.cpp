#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_private/esp_clk.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "soc/rtc.h"
#include "ulp_lp_core.h"

#include "../../common/espnow_packet.hpp"
#include "ulp_main.h"

#include "constants.hpp"
#include <cstdint>

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");

static RTC_DATA_ATTR size_t wakeup_cnt_since_recalibration = 0;
static RTC_DATA_ATTR uint64_t packet_seq = 1;
static RTC_DATA_ATTR uint32_t wheel_boot_id = 0;
static RTC_DATA_ATTR uint32_t slowclk_cal_value = esp_clk_slowclk_cal_get();
static RTC_DATA_ATTR uint64_t cumulative_rotations = 0;
static RTC_DATA_ATTR uint64_t cumulative_ride_time_us = 0;

static uint64_t *shared_periods_buf =
    reinterpret_cast<uint64_t *>(ulp_shared_periods_buf);
static uint32_t &shared_periods_buf_len = ulp_shared_periods_buf_len;
static uint32_t &is_first_wakeup = ulp_is_first_wakeup;
static uint32_t &entered_inactive_state = ulp_entered_inactive_state;

static void init_wifi();
static void init_espnow();
static void init_nvs();
static void init_wheel_boot_id();
static void send_packet(const uint64_t *periods_us, size_t periods_len);
static void recalibrate_slow_clock();

static void wakeup_gpio_init() {
  /* Configure the Hall effect sensor GPIO as input, enable wakeup */
  rtc_gpio_init(SENSOR_PIN);
  rtc_gpio_set_direction(SENSOR_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(SENSOR_PIN);
  rtc_gpio_pullup_en(SENSOR_PIN);
  rtc_gpio_wakeup_enable(SENSOR_PIN, GPIO_INTR_NEGEDGE);
}

static void
start_ulp_program(uint32_t wakeup_source,
                  uint32_t timer_sleep_duration_us = LP_TIMER_DURATION_US) {
  /* Start the program */
  ulp_lp_core_cfg_t cfg = {
      .wakeup_source = wakeup_source,
      .lp_timer_sleep_duration_us = timer_sleep_duration_us,
  };

  ESP_ERROR_CHECK(ulp_lp_core_run(&cfg));
}

static void
init_ulp_program(uint32_t wakeup_source,
                 uint32_t timer_sleep_duration_us = LP_TIMER_DURATION_US) {
  esp_err_t err = ulp_lp_core_load_binary(
      ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
  ESP_ERROR_CHECK(err);
  start_ulp_program(wakeup_source, timer_sleep_duration_us);
}

static void enter_inactive_state() {
  ESP_LOGI(TAG, "Entering inactive state due to inactivity timeout");

  entered_inactive_state = 1;
  is_first_wakeup = 1;
  start_ulp_program(ULP_LP_CORE_WAKEUP_SOURCE_LP_IO);
}

static void exit_inactive_state() {
  ESP_LOGI(TAG, "Exiting inactive state");

  entered_inactive_state = 0;
  start_ulp_program(ULP_LP_CORE_WAKEUP_SOURCE_LP_IO |
                    ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER);
}

static void handle_ulp_wakeup() {
  ++wakeup_cnt_since_recalibration;

  if (entered_inactive_state == 1 ||
      wakeup_cnt_since_recalibration >= WAKEUP_CNT_BEFORE_RECALIBRATION) {
    wakeup_cnt_since_recalibration = 0;
    recalibrate_slow_clock();
  }

  ulp_lp_core_stop();

  const uint32_t buf_len =
      std::min(shared_periods_buf_len, SHARED_PERIODS_BUF_SIZE);

  if (buf_len > 0) {
    uint64_t periods_buf[SHARED_PERIODS_BUF_SIZE] = {};

    memcpy(periods_buf, shared_periods_buf, buf_len * sizeof(periods_buf[0]));
    shared_periods_buf_len = 0;

    if (entered_inactive_state == 1) {
      entered_inactive_state = 0;
    }

    start_ulp_program(ULP_LP_CORE_WAKEUP_SOURCE_LP_IO |
                      ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER);

    uint32_t valid_periods_cnt = 0;
    for (size_t i = 0; i < buf_len; ++i) {
      const uint64_t period_us =
          rtc_time_slowclk_to_us(periods_buf[i], slowclk_cal_value);
      if (period_us < MIN_VALID_PERIOD_US) {
        continue;
      }

      periods_buf[valid_periods_cnt++] = period_us;
    }

    send_packet(periods_buf, valid_periods_cnt);

  } else {
    if (entered_inactive_state == 1) {
      exit_inactive_state();
    } else {
      enter_inactive_state();
    }
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Starting HP core\n");

  wakeup_gpio_init();

  if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_ULP)) {
    ESP_LOGI(TAG, "Woke up from ULP wakeup");

    handle_ulp_wakeup();
  } else {
    ESP_LOGI(TAG, "Not a ULP wakeup, initializing it!");
    init_wheel_boot_id();
    init_ulp_program(ULP_LP_CORE_WAKEUP_SOURCE_LP_IO |
                     ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER);
  }

  ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());

  ESP_LOGI(TAG, "Entering deep sleep");

  uart_wait_tx_idle_polling((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM);
  esp_deep_sleep_start();
}

void init_nvs() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  } else {
    ESP_ERROR_CHECK(ret);
  }
}

void init_wheel_boot_id() {
  init_nvs();

  nvs_handle_t handle{};
  ESP_ERROR_CHECK(nvs_open("wheel", NVS_READWRITE, &handle));

  uint32_t stored_boot_id{};
  const esp_err_t read_err = nvs_get_u32(handle, "boot_id", &stored_boot_id);
  if (read_err != ESP_OK && read_err != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    ESP_ERROR_CHECK(read_err);
  }

  ++stored_boot_id;
  if (stored_boot_id == 0) {
    stored_boot_id = 1;
  }

  ESP_ERROR_CHECK(nvs_set_u32(handle, "boot_id", stored_boot_id));
  ESP_ERROR_CHECK(nvs_commit(handle));
  nvs_close(handle);

  wheel_boot_id = stored_boot_id;
  packet_seq = 1;
  cumulative_rotations = 0;
  cumulative_ride_time_us = 0;

  ESP_LOGI(TAG, "Wheel boot session initialized: boot_id=%lu",
           static_cast<unsigned long>(wheel_boot_id));
}

void init_wifi() {
  init_nvs();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();

  ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void init_espnow() {
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_add_peer(&PEER_INFO));
}

static void send_packet(const uint64_t *periods_us, size_t periods_len) {
  init_wifi();
  init_espnow();

  BikePacket packet = {};
  packet.boot_id = wheel_boot_id;
  packet.seq_num = packet_seq++;
  packet.periods_buf_len =
      static_cast<uint8_t>(std::min(periods_len, max_periods_per_packet));

  memcpy(packet.periods_buf_us, periods_us,
         packet.periods_buf_len * sizeof(packet.periods_buf_us[0]));

  for (size_t i = 0; i < packet.periods_buf_len; ++i) {
    cumulative_ride_time_us += packet.periods_buf_us[i];
  }
  cumulative_rotations += packet.periods_buf_len;
  packet.cumulative_rotations = cumulative_rotations;
  packet.cumulative_ride_time_us = cumulative_ride_time_us;

  ESP_ERROR_CHECK(esp_now_send(DEST_MAC_ADDR.data(),
                               reinterpret_cast<const uint8_t *>(&packet),
                               sizeof(packet)));

  ESP_LOGI(TAG,
           "Sent packet with boot_id=%lu, seq_num=%llu, %u wheel periods, "
           "cumulative rotations=%llu, cumulative ride time=%llu us",
           static_cast<unsigned long>(packet.boot_id), packet.seq_num,
           packet.periods_buf_len, packet.cumulative_rotations,
           packet.cumulative_ride_time_us);
}

static void recalibrate_slow_clock() {
  const uint32_t new_slowclk_cal_value =
      rtc_clk_cal(CLK_CAL_RTC_SLOW, SLOW_CLK_CAL_CYCLES);
  if (new_slowclk_cal_value != slowclk_cal_value &&
      new_slowclk_cal_value != 0) {
    slowclk_cal_value = new_slowclk_cal_value;
    esp_clk_slowclk_cal_set(slowclk_cal_value);
  }
}
