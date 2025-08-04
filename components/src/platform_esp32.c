#include "utils/platform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include <stdlib.h>
#include <string.h>

// ESP32 logging implementations
static void esp32_log_info(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    esp_log_writev(ESP_LOG_INFO, tag, format, args);
    va_end(args);
}

static void esp32_log_error(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    esp_log_writev(ESP_LOG_ERROR, tag, format, args);
    va_end(args);
}

static void esp32_log_warn(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    esp_log_writev(ESP_LOG_WARN, tag, format, args);
    va_end(args);
}

static void esp32_log_debug(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    esp_log_writev(ESP_LOG_DEBUG, tag, format, args);
    va_end(args);
}

// ESP32 network implementation
static int esp32_network_send(const void* data, size_t len, void* addr) {
    // This will be implemented by the specific network handlers
    // For now, return an error to indicate this needs to be handled at a higher level
    return -1;
}

// ESP32 time implementations
static uint32_t esp32_get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint64_t esp32_get_time_us(void) {
    return esp_timer_get_time();
}

// ESP32 memory implementations
static void* esp32_malloc(size_t size) {
    return malloc(size);
}

static void esp32_free(void* ptr) {
    free(ptr);
}

// ESP32 system implementations
static void esp32_system_restart(void) {
    esp_restart();
}

// ESP32 platform interface
static const platform_interface_t esp32_platform = {
    .log_info = esp32_log_info,
    .log_error = esp32_log_error,
    .log_warn = esp32_log_warn,
    .log_debug = esp32_log_debug,
    .network_send = esp32_network_send,
    .get_time_ms = esp32_get_time_ms,
    .get_time_us = esp32_get_time_us,
    .malloc = esp32_malloc,
    .free = esp32_free,
    .system_restart = esp32_system_restart
};

// Global platform interface pointer
const platform_interface_t* platform = NULL;

// Platform initialization function
void platform_init_esp32(void) {
    platform = &esp32_platform;
}