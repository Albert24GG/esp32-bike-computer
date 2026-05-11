#include "constants/board_pins.hpp"
#include "esp_check.h"
#include "esp_sleep.h"
#include "app/app.hpp"
#include "esp_log.h"
#include "rom/uart.h"

#include "driver/gpio.h"
#include <thread>
#include "constants/app_config.hpp"


#include <cstdio>

#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "lvgl.h"

#include "hardware/spi_bus.hpp"
#include "hardware/sd_card.hpp"
#include "hardware/ili9341_display.hpp"
#include "app/lvgl_port_wrapper.hpp"
#include "hardware/xpt2046_touch.hpp"
#include "constants/hw_config.hpp"
#include "app/lvgl_port_wrapper.hpp"


namespace {

using namespace app::graphics;
using namespace hw;
using namespace constants::hw;

constexpr const char* TAG = "app";

esp_err_t set_idle_high(gpio_num_t pin) {
    if (pin == GPIO_NUM_NC) {
        return ESP_OK;
    }

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;

    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config idle-high failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, 1), TAG, "gpio_set_level idle-high failed");

    return ESP_OK;
}

esp_err_t create_demo_ui(LvglPort& lvgl) {
    lvgl.lock();

    auto* screen = lv_scr_act();

    auto* title = lv_label_create(screen);
    lv_label_set_text(title, "ILI9341 + XPT2046 + SD\nESP-IDF + LVGL");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    auto* status = lv_label_create(screen);
    lv_label_set_text(status, "Touch the button");
    lv_obj_align(status, LV_ALIGN_CENTER, 0, -20);

    auto* btn = lv_button_create(screen);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -35);

    auto* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch me");
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(
        btn,
        [](lv_event_t* event) {
            auto* status_label = static_cast<lv_obj_t*>(lv_event_get_user_data(event));

            static int touch_count = 0;
            ++touch_count;

            lv_label_set_text_fmt(status_label, "Button touched: %d", touch_count);
            ESP_LOGI("demo_ui", "Button touched: %d", touch_count);
        },
        LV_EVENT_CLICKED,
        status
    );

    lvgl.unlock();

    return ESP_OK;
}

esp_err_t app_main_impl() {
    SpiBus spi_bus{
        SpiBus::Config{
            .host = lcd::spi_host,
            .mosi = pins::spi_mosi,
            .miso = pins::spi_miso,
            .sclk = pins::spi_sclk,
            .max_transfer_sz = lcd::hres * 80 * sizeof(std::uint16_t),
        }
    };

    ESP_RETURN_ON_ERROR(spi_bus.init(), TAG, "SPI bus init failed");

    // Important before SD init when sharing the SPI bus.
    ESP_RETURN_ON_ERROR(set_idle_high(pins::lcd_cs), TAG, "LCD CS idle-high failed");
    ESP_RETURN_ON_ERROR(set_idle_high(pins::touchscr_cs), TAG, "touch CS idle-high failed");

    SdCard sd{
        SdCard::Config{
            .host = spi_bus.host(),
            .cs = pins::sd_card_cs,
            .mount_point = "/sdcard",
            .max_files = 5,
            .format_if_mount_failed = false,
        }
    };

    // Optional. For first bring-up, you may comment this out until LCD works.
    // ESP_RETURN_ON_ERROR(sd.mount(), TAG, "SD mount failed");

    Ili9341Display lcd{
        Ili9341Display::Config{
            .host = spi_bus.host(),
            .cs = pins::lcd_cs,
            .dc = pins::lcd_dc,
            .rst = pins::lcd_reset,
            .backlight = pins::lcd_backlight,
            .hres = lcd::hres,
            .vres = lcd::vres,
            .pclk_hz = 40 * 1000 * 1000,
            .mirror_x = false,
            .mirror_y = true,
            .swap_xy = false,
            .invert_color = false,
        }
    };

    ESP_RETURN_ON_ERROR(lcd.init(), TAG, "LCD init failed");

    LvglPort lvgl;
    ESP_RETURN_ON_ERROR(lvgl.init(), TAG, "LVGL port init failed");
    ESP_RETURN_ON_ERROR(lvgl.add_display(lcd), TAG, "LVGL display add failed");

    Xpt2046Touch touch{
        Xpt2046Touch::Config{
            .host = spi_bus.host(),
            .cs = pins::touchscr_cs,
            .x_max = lcd::hres,
            .y_max = lcd::vres,
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = true,
            .pclk_hz = 2 * 1000 * 1000,
        }
    };

    ESP_RETURN_ON_ERROR(touch.init(), TAG, "touch init failed");
    ESP_RETURN_ON_ERROR(lvgl.add_touch(touch), TAG, "LVGL touch add failed");

    ESP_RETURN_ON_ERROR(create_demo_ui(lvgl), TAG, "UI creation failed");



    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return ESP_OK;
}

} // namespace


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
    ESP_ERROR_CHECK(app_main_impl());
    // configure_led();
    // app::App app{};

    // ESP_ERROR_CHECK(app.init());

    // const auto wakeup_causes = esp_sleep_get_wakeup_causes();
    // if (wakeup_causes & ESP_SLEEP_WAKEUP_TOUCHPAD) {
    //     ESP_LOGI(constants::app::log_tag, "Woke up from touchpad!");
    //     uart_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
    // }

    // for (int i = 0; i < 10; ++i) {
    //     blink_led();
    //     s_led_state = !s_led_state;
    //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
    // }

    // while (true) {
    //     vTaskDelay(portMAX_DELAY);
    // }

}
