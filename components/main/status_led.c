#include "status_led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"

static const char *TAG = "status_led";

#ifndef CONFIG_STATUS_LED_GPIO
#define CONFIG_STATUS_LED_GPIO 33 // Built-in red LED on ESP32-CAM
#endif

#ifndef CONFIG_STATUS_LED_INVERTED
#define CONFIG_STATUS_LED_INVERTED 1 // Built-in LED is inverted (LOW=ON)
#endif

static TimerHandle_t led_timer = NULL;
static led_state_t current_state = LED_STATE_OFF;
static bool led_on = false;

static void led_set_level(bool on)
{
    led_on = on;
    uint32_t level = CONFIG_STATUS_LED_INVERTED ? (on ? 0 : 1) : (on ? 1 : 0);
    gpio_set_level(CONFIG_STATUS_LED_GPIO, level);
}

static void led_timer_callback(TimerHandle_t xTimer)
{
    switch (current_state)
    {
    case LED_STATE_SLOW_BLINK:
    case LED_STATE_FAST_BLINK:
        led_set_level(!led_on);
        break;
    case LED_STATE_ON:
        led_set_level(true);
        break;
    case LED_STATE_OFF:
    default:
        led_set_level(false);
        break;
    }
}

esp_err_t status_led_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing status LED on GPIO %d", CONFIG_STATUS_LED_GPIO);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CONFIG_STATUS_LED_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure LED GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    led_timer = xTimerCreate("led_timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, led_timer_callback);
    if (led_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create LED timer");
        return ESP_ERR_NO_MEM;
    }

    led_set_level(false);
    current_state = LED_STATE_OFF;

    ESP_LOGI(TAG, "Status LED initialized successfully");
    return ESP_OK;
}

esp_err_t status_led_set_state(led_state_t state)
{
    if (led_timer == NULL)
    {
        ESP_LOGE(TAG, "LED not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    current_state = state;

    xTimerStop(led_timer, 0);

    switch (state)
    {
    case LED_STATE_OFF:
        led_set_level(false);
        ESP_LOGD(TAG, "LED state: OFF");
        break;

    case LED_STATE_ON:
        led_set_level(true);
        ESP_LOGD(TAG, "LED state: ON");
        break;

    case LED_STATE_SLOW_BLINK:
        led_set_level(true);
        xTimerChangePeriod(led_timer, pdMS_TO_TICKS(500), 0); // 1Hz (500ms on/off)
        xTimerStart(led_timer, 0);
        ESP_LOGD(TAG, "LED state: SLOW_BLINK");
        break;

    case LED_STATE_FAST_BLINK:
        led_set_level(true);
        xTimerChangePeriod(led_timer, pdMS_TO_TICKS(100), 0); // 5Hz (100ms on/off)
        xTimerStart(led_timer, 0);
        ESP_LOGD(TAG, "LED state: FAST_BLINK");
        break;

    default:
        ESP_LOGW(TAG, "Unknown LED state: %d", state);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void status_led_deinit(void)
{
    if (led_timer != NULL)
    {
        xTimerStop(led_timer, 0);
        xTimerDelete(led_timer, 0);
        led_timer = NULL;
    }

    led_set_level(false);
    gpio_reset_pin(CONFIG_STATUS_LED_GPIO);

    ESP_LOGI(TAG, "Status LED deinitialized");
}