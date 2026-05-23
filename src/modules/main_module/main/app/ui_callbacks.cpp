

#include "app.hpp"
#include "esp_log.h"
#include "misc/lv_types.h"
#include "ui_events.h"

extern "C" {
void cb_ble_start_pairing(lv_event_t *e) {
    ESP_LOGI("ui_events", "BLE start pairing event triggered");
    app::App::get_instance().on_ble_start_pairing();
}
void cb_exit_screen_maps(lv_event_t *e) {
    ESP_LOGI("ui_events", "Exit screen maps event triggered");
    app::App::get_instance().reset_sleep_timeout();
}
void cb_ble_cancel_pairing(lv_event_t *e) {
    ESP_LOGI("ui_events", "BLE cancel pairing event triggered");
    app::App::get_instance().on_ble_cancel_pairing();
}
void cb_exit_screen_settings(lv_event_t *e) {
    ESP_LOGI("ui_events", "Exit screen settings event triggered");
    app::App::get_instance().on_exit_screen_settings();
}
void cb_reset_trip(lv_event_t *e) {
    ESP_LOGI("ui_events", "Reset trip event triggered");
    app::App::get_instance().on_reset_trip();
}
}
