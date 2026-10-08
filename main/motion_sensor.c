#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "wifi.c"

#define UDP_SERVER_IP "<IP>"  // PC IP on the wifi
#define UDP_SERVER_PORT 5005
#define LED_PIN 2
int wifi_setup_mode = 0;  // 0-> Enter SSID; 1-> Enter Password; 2-> Done
void keyboard_input_task(void* pvParameters) {
  char input_buffer[64];
  wifi_config_t wifi_config = {0};
  int buffer_index = 0;
  while (wifi_setup_mode != 2) {
    switch (wifi_setup_mode) {
      case 0:
        ESP_LOGI(TAG, "Type SSID and press Enter:\n");
        break;
      case 1:
        ESP_LOGI(TAG, "Type Password and press Enter:\n");
        break;
    }

    while (1) {
      // Read a single character from the serial console
      int c = getchar();

      // If a valid character is received (not EOF)
      if (c != EOF) {
        // Check for Carriage Return or Newline (Enter key)
        if (c == '\r' || c == '\n') {
          if (buffer_index > 0) {
            input_buffer[buffer_index] = '\0';  // Null-terminate string
            printf("\nYou typed: %s\n", input_buffer);
            switch (wifi_setup_mode) {
              case 0:
                strncpy((char*)wifi_config.sta.ssid, input_buffer,
                        sizeof(wifi_config.sta.ssid) - 1);
                wifi_setup_mode = 1;
                break;
              case 1:
                strncpy((char*)wifi_config.sta.password, input_buffer,
                        sizeof(wifi_config.sta.password) - 1);
                wifi_setup_mode = 2;
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            }

            buffer_index = 0;  // Reset buffer
          }
        }
        // Check for Backspace to allow corrections
        else if (c == '\b' || c == 127) {
          if (buffer_index > 0) {
            buffer_index--;
            printf("\b \b");  // Erase character from terminal
            fflush(stdout);
          }
        }
        // Store printable characters
        else if (buffer_index < sizeof(input_buffer) - 1) {
          input_buffer[buffer_index++] = c;
          putchar(c);  // Echo character back to the user
          fflush(stdout);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      if (wifi_setup_mode == 2) break;
    }
  }
  vTaskDelay(pdMS_TO_TICKS(5000));

  ESP_LOGI("wifi", "checking status...");
  wifi_csi_config_t csi_config = {
      .lltf_en = false,
      .htltf_en = true,
      .stbc_htltf2_en = false,
      .ltf_merge_en = true,
      .channel_filter_en = true,
      .manu_scale = false,
      .shift = false,
  };

  esp_wifi_set_csi_config(&csi_config);
  esp_wifi_set_csi_rx_cb(csi_callback, csi_queue);
  esp_wifi_set_csi(true);
  if (check_wifi_status()) {
    gpio_set_level(LED_PIN, 1);
  } else {
    gpio_set_level(LED_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    gpio_set_level(LED_PIN, 0);
  }
  vTaskDelete(NULL);
}
void csi_manager(void* pvParameters) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE("CSI", "socket create failed: errno %d", errno);
    vTaskDelete(NULL);
    return;
  }

  struct sockaddr_in dest_addr = {0};
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(UDP_SERVER_PORT);
  dest_addr.sin_addr.s_addr = inet_addr(UDP_SERVER_IP);

  ESP_LOGI("CSI", "sizeof(csiData) = %d", sizeof(csiData));  // should print 394

  csiData latest_frame;
  while (1) {
    xQueueReceive(csi_queue, &latest_frame, portMAX_DELAY);

    int err = sendto(sock, &latest_frame, sizeof(csiData), 0,
                     (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
      ESP_LOGE("CSI", "sendto failed: errno %d", errno);
    }
  }
}
void app_main(void) {
  csi_queue = xQueueCreate(32, sizeof(csiData));

  gpio_reset_pin(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
  esp_err_t response = (wifi_init());
  if (response == ESP_OK) {
    gpio_set_level(LED_PIN, 1);  // LED ON
    vTaskDelay(pdMS_TO_TICKS(800));
    gpio_set_level(LED_PIN, 0);
  }

  TaskHandle_t keyboard_handle = NULL;
  xTaskCreate(keyboard_input_task, "Wifi Setup Input", 4096, NULL, 3,
              &keyboard_handle);
  xTaskCreate(csi_manager, "csi_drain", 4096, NULL, 2, NULL);

  ESP_LOGI(TAG, "aaa");
}
