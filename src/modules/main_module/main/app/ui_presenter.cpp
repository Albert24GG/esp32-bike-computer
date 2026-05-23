#include "ui_presenter.hpp"

#include "ui.h"

#include <cstdio>

namespace app::ui_presenter {
namespace {

constexpr double km_per_mile = 1.609344;
constexpr double mph_per_kmph = 0.621371192237334;

void set_label(lv_obj_t *label, const char *text) noexcept {
  if (label != nullptr) {
    lv_label_set_text(label, text);
  }
}

void set_visible(lv_obj_t *obj, bool visible) noexcept {
  if (obj == nullptr) {
    return;
  }

  if (visible) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void set_button_enabled(lv_obj_t *button, bool enabled) noexcept {
  if (button == nullptr) {
    return;
  }

  if (enabled) {
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_state(button, LV_STATE_DISABLED | LV_STATE_PRESSED |
                                    LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
  } else {
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_state(button, LV_STATE_DISABLED);
  }
}

void write_maps_state(bool no_connected_visible, bool connecting_visible,
                      bool render_visible) noexcept {
  set_visible(ui_MapsNoConnectedContainer, no_connected_visible);
  set_visible(ui_MapsConnectingContainer, connecting_visible);
  set_visible(ui_MapsRenderContainer, render_visible);

  set_button_enabled(ui_ButtonMapsStartPairing, no_connected_visible);
  set_button_enabled(ui_ButtonMapsCancelPairing, connecting_visible);
}

double display_distance(uint64_t distance_mm, UnitSystem unit_system) noexcept {
  const double km = static_cast<double>(distance_mm) / 1'000'000.0;
  return unit_system == UnitSystem::Metric ? km : km / km_per_mile;
}

double display_speed(float speed_kmph, UnitSystem unit_system) noexcept {
  return unit_system == UnitSystem::Metric
             ? static_cast<double>(speed_kmph)
             : static_cast<double>(speed_kmph) * mph_per_kmph;
}

const char *distance_unit(UnitSystem unit_system) noexcept {
  return unit_system == UnitSystem::Metric ? "km" : "mi";
}

const char *speed_unit(UnitSystem unit_system) noexcept {
  return unit_system == UnitSystem::Metric ? "km/h" : "mi/h";
}

void format_time(uint64_t time_us, char *buf, size_t buf_size) noexcept {
  const uint64_t total_seconds = (time_us + 500'000) / 1'000'000;
  const uint64_t hours = total_seconds / 3600;
  const uint64_t minutes = (total_seconds % 3600) / 60;
  const uint64_t seconds = total_seconds % 60;

  if (hours > 0) {
    std::snprintf(buf, buf_size, "%llu:%02llu:%02llu", hours, minutes, seconds);
  } else {
    std::snprintf(buf, buf_size, "%02llu:%02llu", minutes, seconds);
  }
}

uint16_t sleep_timeout_from_dropdown(uint32_t selected,
                                     uint16_t fallback) noexcept {
  const auto &options = constants::app::settings::sleep_timeout_options_s;
  if (selected >= sizeof(options) / sizeof(options[0])) {
    return fallback;
  }

  return options[selected];
}

uint32_t sleep_timeout_to_dropdown(uint16_t timeout_s) noexcept {
  const auto &options = constants::app::settings::sleep_timeout_options_s;
  for (uint32_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
    if (options[i] == timeout_s) {
      return i;
    }
  }

  return 0;
}

} // namespace

void write_metrics(const ride_metrics::RideMetrics::Snapshot &snapshot,
                   const Settings &settings) noexcept {
  char buf[32]{};

  std::snprintf(buf, sizeof(buf), "Trip\n%.1f%s",
                display_distance(snapshot.trip_distance_mm,
                                 settings.unit_system),
                distance_unit(settings.unit_system));
  set_label(ui_LabelTripValue, buf);

  std::snprintf(buf, sizeof(buf), "Avg\n%.1f%s",
                display_speed(snapshot.average_speed_kmph,
                              settings.unit_system),
                speed_unit(settings.unit_system));
  set_label(ui_LabelAvgSpeedValue, buf);

  char time_buf[24]{};
  format_time(snapshot.trip_time_us, time_buf, sizeof(time_buf));
  std::snprintf(buf, sizeof(buf), "Time\n%s", time_buf);
  set_label(ui_LabelRideTimeValue, buf);

  std::snprintf(buf, sizeof(buf), "Total: %.1f%s",
                display_distance(snapshot.total_distance_mm,
                                 settings.unit_system),
                distance_unit(settings.unit_system));
  set_label(ui_LabelTotalDistanceValue, buf);

  std::snprintf(buf, sizeof(buf), "%.1f",
                display_speed(snapshot.current_speed_kmph,
                              settings.unit_system));
  set_label(ui_LabelSpeedValue, buf);
  set_label(ui_LabelSpeedUnit, speed_unit(settings.unit_system));
}

void write_settings(const Settings &settings) noexcept {
  if (ui_DropdownUnits != nullptr) {
    lv_dropdown_set_selected(
        ui_DropdownUnits,
        settings.unit_system == UnitSystem::Metric ? 0 : 1);
  }

  if (ui_SpinboxWheelCircumference != nullptr) {
    lv_spinbox_set_value(ui_SpinboxWheelCircumference,
                         settings.wheel_circumference_mm);
  }

  if (ui_SliderBrightness != nullptr) {
    lv_slider_set_value(ui_SliderBrightness, settings.brightness_percent,
                        LV_ANIM_OFF);
  }

  if (ui_DropdownSleepTimeout != nullptr) {
    lv_dropdown_set_selected(
        ui_DropdownSleepTimeout,
        sleep_timeout_to_dropdown(settings.sleep_timeout_s));
  }
}

Settings read_settings(const Settings &fallback) noexcept {
  Settings settings = fallback;

  if (ui_DropdownUnits != nullptr) {
    settings.unit_system = lv_dropdown_get_selected(ui_DropdownUnits) == 1
                               ? UnitSystem::Imperial
                               : UnitSystem::Metric;
  }

  if (ui_SpinboxWheelCircumference != nullptr) {
    settings.wheel_circumference_mm = static_cast<uint16_t>(
        lv_spinbox_get_value(ui_SpinboxWheelCircumference));
  }

  if (ui_SliderBrightness != nullptr) {
    settings.brightness_percent =
        static_cast<uint8_t>(lv_slider_get_value(ui_SliderBrightness));
  }

  if (ui_DropdownSleepTimeout != nullptr) {
    settings.sleep_timeout_s = sleep_timeout_from_dropdown(
        lv_dropdown_get_selected(ui_DropdownSleepTimeout),
        fallback.sleep_timeout_s);
  }

  return normalize_settings(settings);
}

void write_maps_not_connected() noexcept {
  write_maps_state(true, false, false);
}

void write_maps_connecting() noexcept {
  write_maps_state(false, true, false);
}

void write_maps_connected_waiting() noexcept {
  set_label(ui_LabelMapsCoordinates, "Waiting for coordinates...");
  write_maps_state(false, false, true);
}

void write_maps_location(double latitude, double longitude,
                         float accuracy_m) noexcept {
  char buf[64]{};
  std::snprintf(buf, sizeof(buf), "%.6f° N +/- %.0fm\n%.6f° E",
                latitude, static_cast<double>(accuracy_m), longitude);
  set_label(ui_LabelMapsCoordinates, buf);

  write_maps_state(false, false, true);
}

} // namespace app::ui_presenter
