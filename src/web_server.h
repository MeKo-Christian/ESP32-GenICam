#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

// Web server configuration
#define WEB_SERVER_PORT 80
#define WEB_SERVER_MAX_URI_LEN 512
#define WEB_SERVER_MAX_POST_LEN 1024

// Web server functions
esp_err_t web_server_init(void);
esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);
bool web_server_is_running(void);

// Camera control REST API endpoints
esp_err_t handle_camera_status_get(httpd_req_t *req);
esp_err_t handle_camera_control_get(httpd_req_t *req);
esp_err_t handle_camera_control_post(httpd_req_t *req);
esp_err_t handle_web_interface_get(httpd_req_t *req);