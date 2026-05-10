#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "ulp_lp_core.h"
#include "ulp_main.h"
#include "led_strip.h"
#include "driver/gpio.h"
#include "soc/rtc.h"
#include "esp_private/esp_clk.h"

#include "logger.hpp"
#include "constants.hpp"
#include <cstddef>
#include <cstdint>

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static constexpr auto logger_config = espp::Logger::Config{
    .tag = "WHEEL_MODULE",
    .include_time = true,
    .rate_limit = std::chrono::microseconds(100),
    .level = espp::Logger::Verbosity::DEBUG
};

static const auto logger = espp::Logger(logger_config);

static RTC_DATA_ATTR size_t wakeup_cnt_since_recalibration = 0;
static RTC_DATA_ATTR bool entered_inactive_state = false;


static void wakeup_gpio_init()
{
    /* Configure the button GPIO as input, enable wakeup */
    rtc_gpio_init(SENSOR_PIN);
    rtc_gpio_set_direction(SENSOR_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(SENSOR_PIN);
    rtc_gpio_pullup_en(SENSOR_PIN);
    rtc_gpio_wakeup_enable(SENSOR_PIN, GPIO_INTR_NEGEDGE);
}

static void init_ulp_program(uint32_t wakeup_source, uint32_t timer_sleep_duration_us = LP_TIMER_DURATION_US) {
    esp_err_t err = ulp_lp_core_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    /* Start the program */
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = wakeup_source,
        .lp_timer_sleep_duration_us = timer_sleep_duration_us,
    };

    err = ulp_lp_core_run(&cfg);
    ESP_ERROR_CHECK(err);
}

static uint8_t s_led_state = 0;
static constexpr gpio_num_t BLINK_GPIO = GPIO_NUM_15;

static void blink_led()
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_led()
{
    // ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

static void enter_inactive_state() {
    logger.info("Entering inactive state due to inactivity timeout\n");

    entered_inactive_state = true;
    ulp_lp_core_stop();
    init_ulp_program(ULP_LP_CORE_WAKEUP_SOURCE_LP_IO);
}

static void exit_inactive_state() {
    logger.info("Exiting inactive state\n");

    entered_inactive_state = false;
    ulp_is_first_wakeup = true;
    ulp_lp_core_stop();
    init_ulp_program(ULP_LP_CORE_WAKEUP_SOURCE_LP_IO | ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER);
}

static uint64_t read_uin64_t(uint32_t* addr) {
    return ((uint64_t)addr[1] << 32) | addr[0];
}

static void handle_ulp_wakeup() {
    ++wakeup_cnt_since_recalibration;

    if (entered_inactive_state || wakeup_cnt_since_recalibration >= WAKEUP_CNT_BEFORE_RECALIBRATION) {
        if (entered_inactive_state) {
            exit_inactive_state();
        }
        else {
            wakeup_cnt_since_recalibration = 0;
        }
        rtc_clk_cal(CLK_CAL_RTC_SLOW, SLOW_CLK_CAL_CYCLES);
    }

    if (ulp_s_wheel_periods_buf_len > 0) {
        uint32_t clk_cal_value = esp_clk_slowclk_cal_get();

        size_t buf_len = ulp_s_wheel_periods_buf_len;
        ulp_s_wheel_periods_buf_len = 0;
        uint64_t l_wheel_periods_buf[WHEEL_PERIODS_BUF_SIZE] = {0};
        for (size_t i = 0; i < buf_len; ++i) {
            // Convert the wheel period from slow clock cycles to microseconds
            l_wheel_periods_buf[i] = rtc_time_slowclk_to_us(read_uin64_t(&ulp_s_wheel_periods_buf[2 * i]), clk_cal_value);
        }   


        // Send data via ESP-NOW


        // For now just print debug logs
        for (size_t i = 0; i < buf_len; ++i) {
            logger.debug("Wheel period {}: {} us", i, l_wheel_periods_buf[i]);
        }   
    }
    else {
        // Timeout occurred. Disable the timer LP wakeup source and enter an inactive state
        enter_inactive_state();
    }
}

extern "C" void app_main(void)
{
    logger.info("Starting HP core\n");

    wakeup_gpio_init();
    // configure_led();

    // for (int i = 5; i > 0; i--) {
    //     // printf("Starting in %d seconds... \n", i);
    //     logger.debug("Starting in {} seconds... \n", i);
    //     vTaskDelay(pdMS_TO_TICKS(500));
    //     blink_led();
    //     s_led_state = !s_led_state;
    // }

    if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_ULP)) {
        logger.info("Woke up from ULP wakeup\n");
        handle_ulp_wakeup();
    }
    else {
        logger.info("Not a ULP wakeup, initializing it!\n");
        init_ulp_program(ULP_LP_CORE_WAKEUP_SOURCE_LP_IO | ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER);
    }

    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    // ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(10 * 1000000)); // Wake up after 1 second

    logger.info("Entering deep sleep\n");
    fflush(stdout);

    /* Small delay to ensure the messages are printed */
    vTaskDelay(100 / portTICK_PERIOD_MS);

    esp_deep_sleep_start();

    // while (true) {
    //     if (ulp_s_wheel_periods_buf_len > 0) {
    //         logger.info("Wakeup\n");
    //         handle_ulp_wakeup();
    //     }
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // }

    // esp_restart();
}