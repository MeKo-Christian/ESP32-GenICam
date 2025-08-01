#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "camera_handler.h"
#include "gvcp_handler.h"
#include "gvsp_handler.h"
#include "status_led.h"
#ifdef CONFIG_ENABLE_WEB_SERVER
#include "web_server.h"
#endif

static const char *TAG = "esp32_genicam";

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "ESP32 GenICam Camera starting...");

    ESP_LOGI(TAG, "Initializing status LED...");
    ESP_ERROR_CHECK(status_led_init());

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing WiFi...");
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(wifi_connect());

    ESP_LOGI(TAG, "Initializing camera...");
    ESP_ERROR_CHECK(camera_init());

    ESP_LOGI(TAG, "Initializing GVCP handler...");
    ESP_ERROR_CHECK(gvcp_init());

    ESP_LOGI(TAG, "Initializing GVSP handler...");
    ESP_ERROR_CHECK(gvsp_init());

    ESP_LOGI(TAG, "Creating GVCP task...");
    xTaskCreate(gvcp_task, "gvcp_task", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Creating GVSP task...");
    xTaskCreate(gvsp_task, "gvsp_task", 4096, NULL, 3, NULL);

#ifdef CONFIG_ENABLE_WEB_SERVER
    ESP_LOGI(TAG, "Initializing and starting web server...");
    ESP_ERROR_CHECK(web_server_init());
    ESP_ERROR_CHECK(web_server_start());
#else
    ESP_LOGI(TAG, "Web server disabled (saves memory)");
#endif

    ESP_LOGI(TAG, "ESP32 GenICam Camera initialized successfully");
    status_led_set_state(LED_STATE_ON);
}