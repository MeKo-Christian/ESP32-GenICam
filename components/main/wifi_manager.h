#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

#define WIFI_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_PASS CONFIG_ESP_WIFI_PASSWORD

esp_err_t wifi_init(void);
esp_err_t wifi_connect(void);