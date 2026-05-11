set (SRC "main.cpp" "app.cpp")
idf_component_register(SRCS ${SRC}
                    PRIV_REQUIRES esp_driver_touch_sens esp_driver_uart esp_driver_gpio esp_event esp_timer
                    INCLUDE_DIRS ".")
