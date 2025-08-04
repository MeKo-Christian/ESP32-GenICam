#include "web_server.h"
#include "camera_handler.h"
#include "utils/web_platform.h"
#include "web/web_routes.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>

static const char *TAG = "web_server";
static web_server_handle_t web_server_handle = NULL;

// ESP32-specific request handler wrappers that call platform-independent handlers
static esp_err_t handle_web_interface_wrapper(httpd_req_t *req) {
    return (web_handle_interface_request((web_request_handle_t)req) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t handle_camera_status_wrapper(httpd_req_t *req) {
    return (web_handle_status_request((web_request_handle_t)req) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t handle_camera_control_get_wrapper(httpd_req_t *req) {
    return (web_handle_control_get_request((web_request_handle_t)req) == 0) ? ESP_OK : ESP_FAIL;
}

static esp_err_t handle_camera_control_post_wrapper(httpd_req_t *req) {
    return (web_handle_control_post_request((web_request_handle_t)req) == 0) ? ESP_OK : ESP_FAIL;
}


esp_err_t web_server_init(void) {
    ESP_LOGI(TAG, "Initializing HTTP web server with platform abstraction");
    
    // Initialize web platform for ESP32
    web_platform_init_esp32();
    
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    if (web_server_handle != NULL) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting HTTP web server on port %d", WEB_SERVER_PORT);

    // Start server using platform abstraction
    web_server_handle = web_platform->server_start(WEB_SERVER_PORT);
    if (!web_server_handle) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    // Register URI handlers using platform abstraction and wrapper functions
    int result = 0;
    result |= web_platform->register_handler(web_server_handle, "/", "GET", handle_web_interface_wrapper);
    result |= web_platform->register_handler(web_server_handle, "/api/camera/status", "GET", handle_camera_status_wrapper);
    result |= web_platform->register_handler(web_server_handle, "/api/camera/control", "GET", handle_camera_control_get_wrapper);
    result |= web_platform->register_handler(web_server_handle, "/api/camera/control", "POST", handle_camera_control_post_wrapper);

    if (result != 0) {
        ESP_LOGE(TAG, "Failed to register URI handlers");
        web_platform->server_stop(web_server_handle);
        web_server_handle = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HTTP web server started successfully");
    ESP_LOGI(TAG, "Web interface available at: http://[ESP32_IP_ADDRESS]/");
    ESP_LOGI(TAG, "API endpoints:");
    ESP_LOGI(TAG, "  GET /api/camera/status - Camera status information");
    ESP_LOGI(TAG, "  GET /api/camera/control - Current camera control values");
    ESP_LOGI(TAG, "  POST /api/camera/control - Set camera control values");

    return ESP_OK;
}

esp_err_t web_server_stop(void) {
    if (web_server_handle == NULL) {
        ESP_LOGW(TAG, "Web server not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTP web server");
    int ret = web_platform->server_stop(web_server_handle);
    web_server_handle = NULL;

    if (ret == 0) {
        ESP_LOGI(TAG, "HTTP web server stopped");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to stop HTTP server");
        return ESP_FAIL;
    }
}

bool web_server_is_running(void) {
    return (web_server_handle != NULL);
}

// Legacy function implementations for compatibility
esp_err_t handle_camera_status_get(httpd_req_t *req) {
    return handle_camera_status_wrapper(req);
}

esp_err_t handle_camera_control_get(httpd_req_t *req) {
    return handle_camera_control_get_wrapper(req);
}

esp_err_t handle_camera_control_post(httpd_req_t *req) {
    return handle_camera_control_post_wrapper(req);
}

esp_err_t handle_web_interface_get(httpd_req_t *req) {
    return handle_web_interface_wrapper(req);
}