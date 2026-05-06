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

#include "logger.hpp"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static constexpr auto logger_config = espp::Logger::Config{
    .tag = "WHEEL_MODULE",
    .include_time = true,
    .rate_limit = std::chrono::microseconds(100),
    .level = espp::Logger::Verbosity::DEBUG
};

static const auto logger = espp::Logger(logger_config);

static constexpr gpio_num_t WAKEUP_PIN = GPIO_NUM_0;


static void wakeup_gpio_init()
{
    /* Configure the button GPIO as input, enable wakeup */
    rtc_gpio_init(WAKEUP_PIN);
    rtc_gpio_set_direction(WAKEUP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(WAKEUP_PIN);
    rtc_gpio_pullup_en(WAKEUP_PIN);
    rtc_gpio_wakeup_enable(WAKEUP_PIN, GPIO_INTR_NEGEDGE);
}

static void init_ulp_program() {
    esp_err_t err = ulp_lp_core_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    /* Start the program */
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_IO,
        .lp_timer_sleep_duration_us = 0, // Not used in this example
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

extern "C" void app_main(void)
{
    logger.info("Starting HP core\n");

    wakeup_gpio_init();
    configure_led();

    for (int i = 10; i > 0; i--) {
        // printf("Starting in %d seconds... \n", i);
        logger.debug("Starting in {} seconds... \n", i);
        vTaskDelay(pdMS_TO_TICKS(500));
        blink_led();
        s_led_state = !s_led_state;
    }

    if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_ULP)) {
        logger.info("Woke up from ULP wakeup\n");
    }
    else if (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        logger.info("Woke up from timer wakeup\n");
    }
    else {
        logger.info("Not a ULP wakeup, initializing it!\n");
        init_ulp_program();
    }

    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(10 * 1000000)); // Wake up after 1 second

    logger.info("Entering deep sleep\n");
    fflush(stdout);

    /* Small delay to ensure the messages are printed */
    vTaskDelay(100 / portTICK_PERIOD_MS);

    esp_deep_sleep_start();
}