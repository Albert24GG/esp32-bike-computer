#include "esp_sleep.h"
#include "app/app.hpp"
#include "esp_log.h"
#include "rom/uart.h"

#include "driver/gpio.h"
#include <thread>
#include "constants/app_config.hpp"

static uint8_t s_led_state = 0;
static constexpr gpio_num_t BLINK_GPIO = GPIO_NUM_21;

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

extern "C" void app_main()
{
    configure_led();
    app::App app{};

    ESP_ERROR_CHECK(app.init());

    const auto wakeup_causes = esp_sleep_get_wakeup_causes();
    if (wakeup_causes & ESP_SLEEP_WAKEUP_TOUCHPAD) {
        ESP_LOGI(constants::app::log_tag, "Woke up from touchpad!");
        uart_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
    }

    for (int i = 0; i < 10; ++i) {
        blink_led();
        s_led_state = !s_led_state;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    while (true) {
        vTaskDelay(portMAX_DELAY);
    }

}
