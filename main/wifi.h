#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "stdbool.h"
#define TAG "Wifi Setup"

#define WIFI_AUTHMODE WIFI_AUTH_WPA2_PSK

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const int WIFI_RETRY_ATTEMPT = 3;
static int wifi_retry_count = 0;
static esp_netif_t* wifi_netif = NULL;
static esp_event_handler_instance_t ip_event_handler;
static esp_event_handler_instance_t wifi_event_handler;
wifi_scan_config_t scan_config = {
    .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false};
static EventGroupHandle_t s_wifi_event_group = NULL;
typedef struct {
  int8_t buf[384];
  int64_t timestamp_us;
  uint16_t len;
} __attribute__((packed)) csiData;
QueueHandle_t csi_queue;

void wifi_scan(void);
esp_err_t wifi_init(void);
extern bool check_wifi_status();
