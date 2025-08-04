#include "utils/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>

// Host logging implementations
static void host_log_info(const char* tag, const char* format, ...) {
    printf("[INFO] [%s] ", tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

static void host_log_error(const char* tag, const char* format, ...) {
    fprintf(stderr, "[ERROR] [%s] ", tag);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

static void host_log_warn(const char* tag, const char* format, ...) {
    printf("[WARN] [%s] ", tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

static void host_log_debug(const char* tag, const char* format, ...) {
    printf("[DEBUG] [%s] ", tag);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

// Host network implementation (mock for testing)
static int host_network_send(const void* data, size_t len, void* addr) {
    // Mock implementation for testing
    printf("[MOCK] Network send: %zu bytes\n", len);
    return (int)len;
}

// Host time implementations
static uint32_t host_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static uint64_t host_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec * 1000000 + tv.tv_usec);
}

// Host memory implementations
static void* host_malloc(size_t size) {
    return malloc(size);
}

static void host_free(void* ptr) {
    free(ptr);
}

// Host system implementations
static void host_system_restart(void) {
    printf("[MOCK] System restart requested\n");
    // In host environment, we don't actually restart
}

// Host platform interface
static const platform_interface_t host_platform = {
    .log_info = host_log_info,
    .log_error = host_log_error,
    .log_warn = host_log_warn,
    .log_debug = host_log_debug,
    .network_send = host_network_send,
    .get_time_ms = host_get_time_ms,
    .get_time_us = host_get_time_us,
    .malloc = host_malloc,
    .free = host_free,
    .system_restart = host_system_restart
};

// Platform initialization function
void platform_init_host(void) {
    platform = &host_platform;
}