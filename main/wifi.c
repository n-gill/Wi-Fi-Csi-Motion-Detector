
#include "wifi.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

#include "esp_phy.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

void configure_static_antenna(void) {
  // 1. Map your hardware GPIOs to internal antenna indexes
  esp_phy_ant_gpio_config_t gpio_config = {
      .gpio_cfg = {
          [0] = {.gpio_num = 2, .gpio_select = 1},  // Ant 0 control pin
          [1] = {.gpio_num = 25, .gpio_select = 1}  // Ant 1 control pin
      }};
  esp_phy_set_ant_gpio(&gpio_config);

  // 2. Disable dynamic switching by forcing a single antenna index
  esp_phy_ant_config_t ant_config = {
      .rx_ant_mode = ESP_PHY_ANT_MODE_ANT0,  // Force Rx to Ant 0 only
      .rx_ant_default = ESP_PHY_ANT_ANT0,    // Default Rx fallback
      .tx_ant_mode = ESP_PHY_ANT_MODE_ANT0,  // Force Tx to Ant 0 only
      .enabled_ant0 = 0,                     // Enable Antenna index 0
      .enabled_ant1 = 1                      // Enable Antenna index 1
  };
  esp_phy_set_ant(&ant_config);
}
void csi_callback(void* ctx, wifi_csi_info_t* info) {
  if (info->rx_ctrl.sig_mode != 1) return;

  if (info->len > 384) {
    ESP_LOGI("CSI", "FAILED - len>384");
    ESP_LOGI("CSI", "=== CSI Complex Values (len=%d) ===", info->len);
    ESP_LOGI("CSI", "FAILED len=%d sig_mode=%d stbc=%d mcs=%d cwb=%d",
             info->len, info->rx_ctrl.sig_mode, info->rx_ctrl.stbc,
             info->rx_ctrl.mcs, info->rx_ctrl.cwb);
    return;
  }

  csiData newData = {0};
  memcpy(newData.buf, info->buf, info->len);
  newData.len = info->len;
  newData.timestamp_us = esp_timer_get_time();
  xQueueSend((QueueHandle_t)ctx, &newData, 0);
  ESP_LOGI("CSI", " SUCCESSFUL === CSI Complex Values (len=%d) ===", info->len);
}
static void ip_event_cb(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "Handling IP event, event code 0x%" PRIx32, event_id);
  switch (event_id) {
    case (IP_EVENT_STA_GOT_IP):
      ip_event_got_ip_t* event_ip = (ip_event_got_ip_t*)event_data;
      ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event_ip->ip_info.ip));
      wifi_retry_count = 0;
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
      if (event_base == IP_EVENT) {
        wifi_csi_config_t csi_config = {
            .lltf_en = false,
            .htltf_en = true,
            .stbc_htltf2_en = false,
            .ltf_merge_en = true,
            .channel_filter_en = true,
            .manu_scale = false,
            .shift = false,
        };
        esp_err_t ret;
        ret = esp_wifi_set_csi_config(&csi_config);
        ESP_LOGI(TAG, "csi_config: %s", esp_err_to_name(ret));

        ret = esp_wifi_set_csi_rx_cb(csi_callback, csi_queue);
        ESP_LOGI(TAG, "csi_rx_cb: %s", esp_err_to_name(ret));

        ret = esp_wifi_set_csi(true);
        ESP_LOGI(TAG, "csi_enable: %s", esp_err_to_name(ret));
        wifi_bandwidth_t bw;
        esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
        ESP_LOGI("CSI", "actual bandwidth: %d", bw);
        ESP_LOGI("CSI", "lltf=%d htltf=%d stbc=%d bw=%d", csi_config.lltf_en,
                 csi_config.htltf_en, csi_config.stbc_htltf2_en, bw);
      }

      break;
    case (IP_EVENT_STA_LOST_IP):
      ESP_LOGI(TAG, "Lost IP");
      break;
    case (IP_EVENT_GOT_IP6):
      ip_event_got_ip6_t* event_ip6 = (ip_event_got_ip6_t*)event_data;
      ESP_LOGI(TAG, "Got IPv6: " IPV6STR, IPV62STR(event_ip6->ip6_info.ip));
      wifi_retry_count = 0;
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
      break;
    default:
      ESP_LOGI(TAG, "IP event not handled");
      break;
  }
}

static void wifi_event_cb(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "Handling Wi-Fi event, event code 0x%" PRIx32, event_id);

  switch (event_id) {
    case (WIFI_EVENT_WIFI_READY):
      ESP_LOGI(TAG, "Wi-Fi ready");
      break;
    case (WIFI_EVENT_SCAN_DONE):
      ESP_LOGI(TAG, "Wi-Fi scan done");
      break;
    case (WIFI_EVENT_STA_START):
      ESP_LOGI(TAG, "Wi-Fi started, connecting to AP...");
      esp_wifi_connect();
      break;
    case (WIFI_EVENT_STA_STOP):
      ESP_LOGI(TAG, "Wi-Fi stopped");
      break;
    case (WIFI_EVENT_STA_CONNECTED):
      ESP_LOGI(TAG, "Wi-Fi connected");
      break;
    case (WIFI_EVENT_STA_DISCONNECTED):
      ESP_LOGI(TAG, "Wi-Fi disconnected");
      if (wifi_retry_count < WIFI_RETRY_ATTEMPT) {
        ESP_LOGI(TAG, "Retrying to connect to Wi-Fi network...");
        esp_wifi_connect();
        wifi_retry_count++;
      } else {
        ESP_LOGI(TAG, "Failed to connect to Wi-Fi network");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
      }
      break;
    case (WIFI_EVENT_STA_AUTHMODE_CHANGE):
      ESP_LOGI(TAG, "Wi-Fi authmode changed");
      break;

    default:
      ESP_LOGI(TAG, "Wi-Fi event not handled");
      break;
  }
}

void wifi_scan(void) {
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  wifi_ap_record_t* ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  ESP_LOGI(TAG, "Found %d networks:", ap_count);
  for (int i = 0; i < ap_count; i++) {
    ESP_LOGI(TAG, "[%d] SSID: %s | RSSI: %d | Channel: %d", i,
             ap_records[i].ssid, ap_records[i].rssi, ap_records[i].primary);
  }

  free(ap_records);
}

esp_err_t wifi_init(void) {
  // Initialize Non-Volatile Storage (NVS)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  s_wifi_event_group = xEventGroupCreate();

  ret = esp_netif_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize TCP/IP network stack");
    return ret;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create default event loop");
    return ret;
  }

  ret = esp_wifi_set_default_wifi_sta_handlers();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set default handlers");
    return ret;
  }

  wifi_netif = esp_netif_create_default_wifi_sta();
  if (wifi_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create default WiFi STA interface");
    return ESP_FAIL;
  }

  // Wi-Fi stack configuration parameters
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_cb, NULL, &wifi_event_handler));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_cb, NULL, &ip_event_handler));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
  ESP_ERROR_CHECK(esp_wifi_start());
  wifi_scan();
  return ret;
}
bool check_wifi_status() {
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif && esp_netif_is_netif_up(netif)) {
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
      ESP_LOGI("Wi-Fi", "connected and running.\n");
      return true;
    }
  }
  ESP_LOGI("Wi-Fi", "connection failed\n");
  return false;
}
