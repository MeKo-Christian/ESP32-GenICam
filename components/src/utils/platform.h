#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// Platform abstraction for ESP32 vs host testing
typedef struct {
    // Logging functions
    void (*log_info)(const char* tag, const char* format, ...);
    void (*log_error)(const char* tag, const char* format, ...);
    void (*log_warn)(const char* tag, const char* format, ...);
    void (*log_debug)(const char* tag, const char* format, ...);
    
    // Network functions
    int (*network_send)(const void* data, size_t len, void* addr);
    
    // Time functions
    uint32_t (*get_time_ms)(void);
    uint64_t (*get_time_us)(void);
    
    // Memory functions
    void* (*malloc)(size_t size);
    void (*free)(void* ptr);
    
    // System functions
    void (*system_restart)(void);
} platform_interface_t;

// Global platform interface pointer
extern const platform_interface_t* platform;

// Platform initialization functions
void platform_init_esp32(void);
void platform_init_host(void);

#endif // PLATFORM_H