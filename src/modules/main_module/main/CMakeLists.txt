idf_component_register(SRC_DIRS "." "app/" "hardware/"
                    PRIV_REQUIRES
                        esp_driver_touch_sens
                        esp_driver_uart
                        esp_driver_gpio
                        esp_event
                        esp_timer
                        esp_lcd
                        esp_lvgl_port
                        esp_lcd_ili9341
                        esp_lcd_touch_xpt2046
                        lvgl
                        fatfs
                        sdmmc
                    INCLUDE_DIRS ".")
