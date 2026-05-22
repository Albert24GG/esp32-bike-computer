#include "persistent_store.hpp"

#include "constants/app_config.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

namespace app {
namespace {

esp_err_t read_u8(nvs_handle_t handle, const char *key, uint8_t &value) {
  const esp_err_t err = nvs_get_u8(handle, key, &value);
  return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t read_u16(nvs_handle_t handle, const char *key, uint16_t &value) {
  const esp_err_t err = nvs_get_u16(handle, key, &value);
  return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t read_u64(nvs_handle_t handle, const char *key, uint64_t &value) {
  const esp_err_t err = nvs_get_u64(handle, key, &value);
  return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

} // namespace

PersistentStore::~PersistentStore() noexcept {
  if (initialized_) {
    nvs_close(handle_);
  }
}

esp_err_t PersistentStore::init() noexcept {
  if (initialized_) {
    return ESP_OK;
  }

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), tag_, "nvs erase failed");
    ESP_RETURN_ON_ERROR(nvs_flash_init(), tag_, "nvs init failed");
  } else {
    ESP_RETURN_ON_ERROR(ret, tag_, "nvs init failed");
  }

  ESP_RETURN_ON_ERROR(nvs_open(namespace_name_, NVS_READWRITE, &handle_), tag_,
                      "nvs open failed");

  initialized_ = true;
  ESP_RETURN_ON_ERROR(ensure_schema(), tag_, "nvs schema setup failed");

  return ESP_OK;
}

esp_err_t PersistentStore::ensure_schema() noexcept {
  uint32_t schema_version = 0;
  const esp_err_t err = nvs_get_u32(handle_, "schema", &schema_version);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_RETURN_ON_ERROR(
        nvs_set_u32(handle_, "schema", constants::app::persistence::schema_version),
        tag_, "schema write failed");
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), tag_, "schema commit failed");
    return ESP_OK;
  }

  ESP_RETURN_ON_ERROR(err, tag_, "schema read failed");
  if (schema_version != constants::app::persistence::schema_version) {
    ESP_LOGW(tag_, "Unsupported NVS schema %lu, resetting app namespace",
             static_cast<unsigned long>(schema_version));
    ESP_RETURN_ON_ERROR(nvs_erase_all(handle_), tag_, "namespace erase failed");
    ESP_RETURN_ON_ERROR(
        nvs_set_u32(handle_, "schema", constants::app::persistence::schema_version),
        tag_, "schema rewrite failed");
    ESP_RETURN_ON_ERROR(nvs_commit(handle_), tag_, "schema rewrite commit failed");
  }

  return ESP_OK;
}

esp_err_t PersistentStore::load_settings(Settings &settings) noexcept {
  Settings loaded{};
  uint8_t unit = static_cast<uint8_t>(loaded.unit_system);

  ESP_RETURN_ON_ERROR(read_u8(handle_, "unit", unit), tag_, "unit read failed");
  ESP_RETURN_ON_ERROR(read_u16(handle_, "wheel_mm", loaded.wheel_circumference_mm),
                      tag_, "wheel circumference read failed");
  ESP_RETURN_ON_ERROR(read_u8(handle_, "bright", loaded.brightness_percent),
                      tag_, "brightness read failed");
  ESP_RETURN_ON_ERROR(read_u16(handle_, "sleep_s", loaded.sleep_timeout_s),
                      tag_, "sleep timeout read failed");

  loaded.unit_system = unit == static_cast<uint8_t>(UnitSystem::Imperial)
                           ? UnitSystem::Imperial
                           : UnitSystem::Metric;
  settings = normalize_settings(loaded);
  return ESP_OK;
}

esp_err_t PersistentStore::save_settings(const Settings &settings) noexcept {
  const Settings normalized = normalize_settings(settings);

  ESP_RETURN_ON_ERROR(
      nvs_set_u8(handle_, "unit", static_cast<uint8_t>(normalized.unit_system)),
      tag_, "unit write failed");
  ESP_RETURN_ON_ERROR(
      nvs_set_u16(handle_, "wheel_mm", normalized.wheel_circumference_mm), tag_,
      "wheel circumference write failed");
  ESP_RETURN_ON_ERROR(nvs_set_u8(handle_, "bright", normalized.brightness_percent),
                      tag_, "brightness write failed");
  ESP_RETURN_ON_ERROR(nvs_set_u16(handle_, "sleep_s", normalized.sleep_timeout_s),
                      tag_, "sleep timeout write failed");
  ESP_RETURN_ON_ERROR(nvs_commit(handle_), tag_, "settings commit failed");

  return ESP_OK;
}

esp_err_t PersistentStore::load_ride_state(PersistentRideState &state) noexcept {
  PersistentRideState loaded{};

  ESP_RETURN_ON_ERROR(read_u64(handle_, "trip_mm", loaded.trip_distance_mm),
                      tag_, "trip distance read failed");
  ESP_RETURN_ON_ERROR(read_u64(handle_, "trip_us", loaded.trip_time_us), tag_,
                      "trip time read failed");
  ESP_RETURN_ON_ERROR(read_u64(handle_, "total_mm", loaded.total_distance_mm),
                      tag_, "total distance read failed");
  ESP_RETURN_ON_ERROR(read_u64(handle_, "wheel_rot",
                               loaded.wheel_cumulative_rotations),
                      tag_, "wheel rotations read failed");
  ESP_RETURN_ON_ERROR(read_u64(handle_, "wheel_time",
                               loaded.wheel_cumulative_ride_time_us),
                      tag_, "wheel time read failed");

  state = loaded;
  return ESP_OK;
}

esp_err_t
PersistentStore::save_ride_state(const PersistentRideState &state) noexcept {
  ESP_RETURN_ON_ERROR(nvs_set_u64(handle_, "trip_mm", state.trip_distance_mm),
                      tag_, "trip distance write failed");
  ESP_RETURN_ON_ERROR(nvs_set_u64(handle_, "trip_us", state.trip_time_us), tag_,
                      "trip time write failed");
  ESP_RETURN_ON_ERROR(nvs_set_u64(handle_, "total_mm", state.total_distance_mm),
                      tag_, "total distance write failed");
  ESP_RETURN_ON_ERROR(nvs_set_u64(handle_, "wheel_rot",
                                  state.wheel_cumulative_rotations),
                      tag_, "wheel rotations write failed");
  ESP_RETURN_ON_ERROR(nvs_set_u64(handle_, "wheel_time",
                                  state.wheel_cumulative_ride_time_us),
                      tag_, "wheel time write failed");
  ESP_RETURN_ON_ERROR(nvs_commit(handle_), tag_, "ride state commit failed");

  return ESP_OK;
}

ride_metrics::RideMetrics::InitialState
to_ride_initial_state(const PersistentRideState &state) noexcept {
  return {
      .trip_distance_mm = state.trip_distance_mm,
      .trip_time_us = state.trip_time_us,
      .total_distance_mm = state.total_distance_mm,
  };
}

} // namespace app
