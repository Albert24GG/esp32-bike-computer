#include "ble_location_service.hpp"

#include "constants/app_config.hpp"

#include "esp_check.h"
#include "esp_coexist.h"
#include "esp_log.h"
#include "esp_timer.h"

// ESP-IDF 6.0.1's Xtensa FreeRTOS port can miss this macro while compiling
// NimBLE through gnu++26, even though the target is dual-core.
#ifndef portYIELD_CORE
#define portYIELD_CORE(xCoreID) vPortYieldOtherCore(xCoreID)
#endif

#include "esp_bt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

extern "C" void ble_store_config_init(void);

namespace app {
namespace {

constexpr const char *tag = "ble_location";
constexpr const char *device_name = "BikeComputer";
constexpr uint16_t no_connection = BLE_HS_CONN_HANDLE_NONE;

// Nordic UART-compatible UUIDs used by the Android reference template.
const ble_uuid128_t location_service_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5,
    0x01, 0x00, 0x40, 0x6e);
const ble_uuid128_t location_rx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5,
    0x02, 0x00, 0x40, 0x6e);
const ble_uuid128_t location_tx_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5,
    0x03, 0x00, 0x40, 0x6e);

uint16_t rx_value_handle{};
uint16_t tx_value_handle{};
BleLocationService *active_service{};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &location_service_uuid.u,
        .characteristics =
            (ble_gatt_chr_def[]){
                {
                    .uuid = &location_rx_uuid.u,
                    .access_cb = BleLocationService::gatt_access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                    .val_handle = &rx_value_handle,
                },
                {
                    .uuid = &location_tx_uuid.u,
                    .access_cb = BleLocationService::gatt_access_cb,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &tx_value_handle,
                },
                {},
            },
    },
    {},
};
#pragma GCC diagnostic pop

void restart_advertising_if_requested(BleLocationService &service) noexcept {
  const esp_err_t err = service.start_pairing();
  if (err != ESP_OK) {
    ESP_LOGW(tag, "failed to restart BLE advertising: %s",
             esp_err_to_name(err));
  }
}

void format_addr(char *addr_str, size_t addr_str_size,
                 const uint8_t addr[6]) noexcept {
  std::snprintf(addr_str, addr_str_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

void print_conn_desc(const ble_gap_conn_desc &desc) noexcept {
  char local_addr[18]{};
  char peer_addr[18]{};
  format_addr(local_addr, sizeof(local_addr), desc.our_id_addr.val);
  format_addr(peer_addr, sizeof(peer_addr), desc.peer_id_addr.val);

  ESP_LOGI(tag,
           "BLE connected: conn_handle=%u local=%s peer=%s interval=%u "
           "latency=%u supervision_timeout=%u encrypted=%u bonded=%u",
           desc.conn_handle, local_addr, peer_addr, desc.conn_itvl,
           desc.conn_latency, desc.supervision_timeout,
           desc.sec_state.encrypted, desc.sec_state.bonded);
}

bool parse_location(const char *payload, LocationCoordinates &location) noexcept {
  double lat{};
  double lon{};
  double accuracy{};
  const int matched = std::sscanf(payload, " %lf , %lf , %lf", &lat, &lon,
                                  &accuracy);
  if (matched < 2 || lat < -90.0 || lat > 90.0 || lon < -180.0 ||
      lon > 180.0) {
    return false;
  }

  location.latitude = lat;
  location.longitude = lon;
  location.accuracy_m = matched >= 3 ? static_cast<float>(std::max(0.0, accuracy))
                                     : 0.0F;
  location.timestamp_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
  return true;
}

void configure_ble_tx_power() noexcept {
  const esp_power_level_t power_level = ESP_PWR_LVL_P3;
  esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, power_level);
  if (err != ESP_OK) {
    ESP_LOGW(tag, "failed to set default BLE TX power: %s",
             esp_err_to_name(err));
  }

  err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, power_level);
  if (err != ESP_OK) {
    ESP_LOGW(tag, "failed to set advertising BLE TX power: %s",
             esp_err_to_name(err));
  } else {
    ESP_LOGI(tag, "BLE TX power set to +3 dBm");
  }
}

void configure_connection_tx_power(uint16_t conn_handle) noexcept {
  if (conn_handle > 8) {
    return;
  }

  const auto power_type =
      static_cast<esp_ble_power_type_t>(ESP_BLE_PWR_TYPE_CONN_HDL0 +
                                        conn_handle);
  const esp_err_t err = esp_ble_tx_power_set(power_type, ESP_PWR_LVL_P3);
  if (err != ESP_OK) {
    ESP_LOGW(tag, "failed to set BLE connection TX power: %s",
             esp_err_to_name(err));
  }
}

void request_low_power_connection_params(uint16_t conn_handle) noexcept {
  ble_gap_upd_params params{};
  params.itvl_min = 120; // 150 ms
  params.itvl_max = 240; // 300 ms
  params.latency = 4;
  params.supervision_timeout = 600; // 6 s
  params.min_ce_len = 0;
  params.max_ce_len = 0;

  const int rc = ble_gap_update_params(conn_handle, &params);
  if (rc != 0) {
    ESP_LOGW(tag, "failed to request BLE low-power connection params: %d", rc);
  } else {
    ESP_LOGI(tag, "requested BLE low-power connection params");
  }
}

} // namespace

esp_err_t BleLocationService::init(const Config &config) noexcept {
  if (initialized_) {
    return ESP_OK;
  }

  config_ = config;
  active_service = this;
  conn_handle_ = no_connection;

  ESP_RETURN_ON_ERROR(esp_coex_preference_set(ESP_COEX_PREFER_BALANCE), tag,
                      "failed to set WiFi/BLE coexistence preference");
  ESP_LOGI(tag, "WiFi/BLE coexistence preference set to balanced");

  int rc = nimble_port_init();
  ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                      "failed to initialize NimBLE host: %d", rc);
  configure_ble_tx_power();

  ble_svc_gap_init();
  ble_svc_gatt_init();

  rc = ble_svc_gap_device_name_set(device_name);
  ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                      "failed to set BLE device name: %d", rc);

  rc = ble_gatts_count_cfg(gatt_services);
  ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                      "failed to count GATT services: %d", rc);

  rc = ble_gatts_add_svcs(gatt_services);
  ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                      "failed to add GATT services: %d", rc);

  ble_hs_cfg.reset_cb = BleLocationService::on_stack_reset;
  ble_hs_cfg.sync_cb = BleLocationService::on_stack_sync;
  ble_hs_cfg.gatts_register_cb = BleLocationService::gatt_register_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_store_config_init();

  nimble_port_freertos_init(BleLocationService::nimble_host_task);

  initialized_ = true;
  advertising_requested_ = false;
  ESP_LOGI(tag, "BLE location service initialized as '%s'", device_name);

  return ESP_OK;
}

esp_err_t BleLocationService::start_pairing() noexcept {
  advertising_requested_ = true;
  if (!host_synced_) {
    ESP_LOGI(tag, "BLE pairing requested; waiting for NimBLE host sync");
    return ESP_OK;
  }

  return start_advertising();
}

esp_err_t BleLocationService::cancel_pairing() noexcept {
  advertising_requested_ = false;
  if (!host_synced_) {
    notify_state(BleLocationState::Idle);
    return ESP_OK;
  }

  if (ble_gap_adv_active()) {
    const int rc = ble_gap_adv_stop();
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                        "failed to stop BLE advertising: %d", rc);
    ESP_LOGI(tag, "BLE advertising stopped");
  }

  notify_state(BleLocationState::Idle);
  return ESP_OK;
}

void BleLocationService::on_stack_reset(int reason) noexcept {
  ESP_LOGW(tag, "NimBLE stack reset: reason=%d", reason);
}

void BleLocationService::on_stack_sync() noexcept {
  if (active_service == nullptr) {
    return;
  }

  BleLocationService &self = *active_service;
  self.host_synced_ = true;

  int rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(tag, "no available BLE address: %d", rc);
    return;
  }

  rc = ble_hs_id_infer_auto(0, &self.own_addr_type_);
  if (rc != 0) {
    ESP_LOGE(tag, "failed to infer BLE address type: %d", rc);
    return;
  }

  rc = ble_hs_id_copy_addr(self.own_addr_type_, self.own_addr_, nullptr);
  if (rc != 0) {
    ESP_LOGE(tag, "failed to copy BLE address: %d", rc);
    return;
  }

  char addr_str[18]{};
  format_addr(addr_str, sizeof(addr_str), self.own_addr_);
  ESP_LOGI(tag, "BLE host synced; address=%s", addr_str);

  if (self.advertising_requested_) {
    const esp_err_t err = self.start_advertising();
    if (err != ESP_OK) {
      ESP_LOGE(tag, "failed to start BLE advertising after sync: %s",
               esp_err_to_name(err));
    }
  }
}

void BleLocationService::nimble_host_task(void *param) noexcept {
  ESP_LOGI(tag, "NimBLE host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

int BleLocationService::gap_event_handler(ble_gap_event *event,
                                          void *arg) noexcept {
  BleLocationService &self = *static_cast<BleLocationService *>(arg);

  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      ble_gap_conn_desc desc{};
      const int rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
      if (rc == 0) {
        print_conn_desc(desc);
      } else {
        ESP_LOGW(tag, "BLE connected, but conn descriptor lookup failed: %d",
                 rc);
      }
      self.connected_ = true;
      self.conn_handle_ = event->connect.conn_handle;
      configure_connection_tx_power(event->connect.conn_handle);
      request_low_power_connection_params(event->connect.conn_handle);
      self.notify_state(BleLocationState::Connected);
      self.notify_status("connected");
    } else {
      ESP_LOGW(tag, "BLE connection failed: status=%d", event->connect.status);
      if (self.advertising_requested_) {
        restart_advertising_if_requested(self);
      }
    }
    return 0;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(tag, "BLE disconnected: reason=%d", event->disconnect.reason);
    self.connected_ = false;
    self.conn_handle_ = no_connection;
    self.tx_subscribed_ = false;
    if (self.advertising_requested_) {
      restart_advertising_if_requested(self);
    } else {
      self.notify_state(BleLocationState::Idle);
    }
    return 0;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    ESP_LOGI(tag, "BLE advertising completed: reason=%d",
             event->adv_complete.reason);
    if (self.advertising_requested_) {
      restart_advertising_if_requested(self);
    }
    return 0;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(tag, "BLE MTU updated: conn_handle=%u mtu=%u",
             event->mtu.conn_handle, event->mtu.value);
    return 0;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(tag,
             "BLE subscribe: conn_handle=%u attr_handle=%u notify=%u "
             "indicate=%u",
             event->subscribe.conn_handle, event->subscribe.attr_handle,
             event->subscribe.cur_notify, event->subscribe.cur_indicate);
    if (event->subscribe.attr_handle == tx_value_handle) {
      self.tx_subscribed_ = event->subscribe.cur_notify != 0;
    }
    return 0;

  case BLE_GAP_EVENT_NOTIFY_TX:
    if (event->notify_tx.status != 0 &&
        event->notify_tx.status != BLE_HS_EDONE) {
      ESP_LOGW(tag, "BLE notify failed: conn_handle=%u attr_handle=%u status=%d",
               event->notify_tx.conn_handle, event->notify_tx.attr_handle,
               event->notify_tx.status);
    }
    return 0;

  default:
    return 0;
  }
}

int BleLocationService::gatt_access_cb(uint16_t conn_handle,
                                       uint16_t attr_handle,
                                       ble_gatt_access_ctxt *ctxt,
                                       void *arg) noexcept {
  if (active_service == nullptr) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
      attr_handle != rx_value_handle) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  char payload[80]{};
  uint16_t copied_len{};
  const int rc =
      ble_hs_mbuf_to_flat(ctxt->om, payload, sizeof(payload) - 1, &copied_len);
  if (rc != 0) {
    ESP_LOGW(tag, "failed to read BLE location payload: %d", rc);
    return BLE_ATT_ERR_UNLIKELY;
  }
  payload[copied_len] = '\0';

  ESP_LOGI(tag, "BLE location payload received: '%s'", payload);
  active_service->handle_location_write(payload, copied_len);

  return 0;
}

void BleLocationService::gatt_register_cb(ble_gatt_register_ctxt *ctxt,
                                          void *arg) noexcept {
  char uuid[BLE_UUID_STR_LEN]{};
  switch (ctxt->op) {
  case BLE_GATT_REGISTER_OP_SVC:
    ESP_LOGD(tag, "registered BLE service %s handle=%u",
             ble_uuid_to_str(ctxt->svc.svc_def->uuid, uuid), ctxt->svc.handle);
    break;
  case BLE_GATT_REGISTER_OP_CHR:
    ESP_LOGD(tag, "registered BLE characteristic %s value_handle=%u",
             ble_uuid_to_str(ctxt->chr.chr_def->uuid, uuid),
             ctxt->chr.val_handle);
    break;
  default:
    break;
  }
}

esp_err_t BleLocationService::start_advertising() noexcept {
  if (!host_synced_) {
    advertising_requested_ = true;
    return ESP_OK;
  }

  if (connected_) {
    ESP_LOGI(tag, "BLE phone already connected");
    notify_state(BleLocationState::Connected);
    return ESP_OK;
  }

  if (ble_gap_adv_active()) {
    ESP_LOGI(tag, "BLE advertising already active");
    notify_state(BleLocationState::Advertising);
    return ESP_OK;
  }

  ble_hs_adv_fields adv_fields{};
  adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  adv_fields.tx_pwr_lvl_is_present = 1;
  adv_fields.le_role = 0x00;
  adv_fields.le_role_is_present = 1;
  adv_fields.uuids128 = &location_service_uuid;
  adv_fields.num_uuids128 = 1;
  adv_fields.uuids128_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&adv_fields);
  ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                      "failed to set BLE advertising fields: %d", rc);

  const char *name = ble_svc_gap_device_name();
  ble_hs_adv_fields rsp_fields{};
  rsp_fields.name = reinterpret_cast<const uint8_t *>(name);
  rsp_fields.name_len = std::strlen(name);
  rsp_fields.name_is_complete = 1;
  rsp_fields.device_addr = own_addr_;
  rsp_fields.device_addr_type = own_addr_type_;
  rsp_fields.device_addr_is_present = 1;

  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                      "failed to set BLE scan response fields: %d", rc);

  ble_gap_adv_params adv_params{};
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
  adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(120);

  rc = ble_gap_adv_start(own_addr_type_, nullptr, BLE_HS_FOREVER, &adv_params,
                         BleLocationService::gap_event_handler, this);
  ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, tag,
                      "failed to start BLE advertising: %d", rc);

  ESP_LOGI(tag, "BLE advertising started; waiting for Android phone");
  notify_state(BleLocationState::Advertising);
  return ESP_OK;
}

void BleLocationService::handle_location_write(const char *payload,
                                               uint16_t len) noexcept {
  LocationCoordinates location{};
  if (!parse_location(payload, location)) {
    ESP_LOGW(tag,
             "invalid BLE location payload; expected 'latitude,longitude,"
             "accuracy_m'");
    notify_status("invalid_location");
    return;
  }

  ESP_LOGI(tag, "BLE location parsed: lat=%.6f lon=%.6f accuracy=%.1fm",
           location.latitude, location.longitude,
           static_cast<double>(location.accuracy_m));

  if (config_.location_callback != nullptr) {
    config_.location_callback(location, config_.location_callback_ctx);
  }

  notify_status("location_ok");
}

void BleLocationService::notify_status(const char *status) noexcept {
  if (!connected_ || !tx_subscribed_ || conn_handle_ == no_connection ||
      tx_value_handle == 0) {
    return;
  }

  os_mbuf *om = ble_hs_mbuf_from_flat(status, std::strlen(status));
  if (om == nullptr) {
    ESP_LOGW(tag, "failed to allocate BLE notification payload");
    return;
  }

  const int rc = ble_gatts_notify_custom(conn_handle_, tx_value_handle, om);
  if (rc != 0) {
    ESP_LOGW(tag, "failed to send BLE status notification '%s': %d", status,
             rc);
  }
}

void BleLocationService::notify_state(BleLocationState state) noexcept {
  if (config_.state_callback != nullptr) {
    config_.state_callback(state, config_.state_callback_ctx);
  }
}

} // namespace app
