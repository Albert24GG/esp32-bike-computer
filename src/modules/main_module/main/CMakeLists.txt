idf_component_register(SRC_DIRS "." "app/" "hardware/"
                    PRIV_REQUIRES
                        esp_driver_touch_sens
                        esp_driver_uart
                        esp_driver_gpio
                        esp_driver_ledc
                        freertos
                        esp_event
                        esp_timer
                        esp_lcd
                        esp_lvgl_port
                        esp_lcd_ili9341
                        esp_lcd_touch_xpt2046
                        lvgl
                        fatfs
                        sdmmc
                        nvs_flash
                        esp_wifi
                        bt
                        esp_coex
                        ui
                    INCLUDE_DIRS "."
                    WHOLE_ARCHIVE
                    )
