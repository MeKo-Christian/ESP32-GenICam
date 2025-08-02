#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        LED_STATE_OFF = 0,
        LED_STATE_ON,
        LED_STATE_SLOW_BLINK, // 1Hz - WiFi connecting
        LED_STATE_FAST_BLINK  // 5Hz - Streaming active
    } led_state_t;

    esp_err_t status_led_init(void);
    esp_err_t status_led_set_state(led_state_t state);
    void status_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // STATUS_LED_H