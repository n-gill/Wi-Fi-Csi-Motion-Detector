#pragma once
#include <stdio.h>

#include <wifi.c>

#include "driver/gpio.h"
#include "esp_wifi.h"
void keyboard_input_task(void* pvParameters);
void app_main(void);
