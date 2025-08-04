#ifndef WEB_PLATFORM_H
#define WEB_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../web/web_api.h"

// Forward declarations for platform-specific types
typedef void* web_server_handle_t;
typedef void* web_request_handle_t;

// Web platform interface for abstracting HTTP server operations
typedef struct {
    // HTTP server operations
    web_server_handle_t (*server_start)(int port);
    int (*server_stop)(web_server_handle_t server);
    int (*register_handler)(web_server_handle_t server, const char* uri, const char* method, void* handler_func);
    
    // HTTP request/response operations  
    int (*send_response)(web_request_handle_t req, const char* content_type, const char* data, size_t len);
    int (*send_error)(web_request_handle_t req, int status_code);
    int (*receive_data)(web_request_handle_t req, char* buffer, size_t max_len);
    const char* (*get_request_uri)(web_request_handle_t req);
    size_t (*get_content_length)(web_request_handle_t req);
    
    // Camera interface callbacks
    void (*get_camera_status)(web_camera_status_t* status);
    void (*get_camera_params)(web_camera_params_t* params);
    int (*set_camera_param)(const char* param_name, int value);
} web_platform_interface_t;

// Global web platform interface pointer
extern const web_platform_interface_t* web_platform;

// Platform initialization functions
void web_platform_init_esp32(void);
void web_platform_init_host(void);

// Platform-independent web request handlers
int web_handle_interface_request(web_request_handle_t req);
int web_handle_status_request(web_request_handle_t req);
int web_handle_control_get_request(web_request_handle_t req);
int web_handle_control_post_request(web_request_handle_t req);

#endif // WEB_PLATFORM_H