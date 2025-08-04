#include "web_platform.h"
#include "platform.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "web_platform_esp32";

// ESP32 HTTP server implementation
static web_server_handle_t esp32_server_start(int port) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    config.server_port = port;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        platform->log_error(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return NULL;
    }
    
    return (web_server_handle_t)server;
}

static int esp32_server_stop(web_server_handle_t server) {
    if (!server) return -1;
    
    esp_err_t ret = httpd_stop((httpd_handle_t)server);
    return (ret == ESP_OK) ? 0 : -1;
}

static int esp32_register_handler(web_server_handle_t server, const char* uri, const char* method, void* handler_func) {
    if (!server || !uri || !method || !handler_func) return -1;
    
    httpd_uri_t uri_handler = {
        .uri = uri,
        .method = (strcmp(method, "POST") == 0) ? HTTP_POST : HTTP_GET,
        .handler = (esp_err_t(*)(httpd_req_t*))handler_func,
        .user_ctx = NULL
    };
    
    esp_err_t ret = httpd_register_uri_handler((httpd_handle_t)server, &uri_handler);
    return (ret == ESP_OK) ? 0 : -1;
}

static int esp32_send_response(web_request_handle_t req, const char* content_type, const char* data, size_t len) {
    if (!req || !data) return -1;
    
    httpd_req_t* request = (httpd_req_t*)req;
    
    if (content_type) {
        httpd_resp_set_type(request, content_type);
    }
    
    esp_err_t ret = httpd_resp_send(request, data, len);
    return (ret == ESP_OK) ? 0 : -1;
}

static int esp32_send_error(web_request_handle_t req, int status_code) {
    if (!req) return -1;
    
    httpd_req_t* request = (httpd_req_t*)req;
    
    switch (status_code) {
        case 400:
            return (httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Bad Request") == ESP_OK) ? 0 : -1;
        case 404:
            return (httpd_resp_send_404(request) == ESP_OK) ? 0 : -1;
        case 500:
            return (httpd_resp_send_500(request) == ESP_OK) ? 0 : -1;
        default:
            return (httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal Error") == ESP_OK) ? 0 : -1;
    }
}

static int esp32_receive_data(web_request_handle_t req, char* buffer, size_t max_len) {
    if (!req || !buffer) return -1;
    
    httpd_req_t* request = (httpd_req_t*)req;
    size_t recv_size = (request->content_len < max_len) ? request->content_len : max_len - 1;
    
    int ret = httpd_req_recv(request, buffer, recv_size);
    return ret;
}

static const char* esp32_get_request_uri(web_request_handle_t req) {
    if (!req) return NULL;
    
    httpd_req_t* request = (httpd_req_t*)req;
    return request->uri;
}

static size_t esp32_get_content_length(web_request_handle_t req) {
    if (!req) return 0;
    
    httpd_req_t* request = (httpd_req_t*)req;
    return request->content_len;
}

// Forward declarations for camera handler functions
extern bool camera_is_real_camera_active(void);
extern uint32_t camera_get_max_payload_size(void);
extern uint32_t camera_get_genicam_pixformat(void);
extern uint32_t camera_get_exposure_time(void);
extern int camera_get_gain(void);
extern int camera_get_brightness(void);
extern int camera_get_contrast(void);
extern int camera_get_saturation(void);
extern int camera_get_white_balance_mode(void);
extern int camera_get_trigger_mode(void);
extern int camera_get_jpeg_quality(void);
extern int camera_set_exposure_time(uint32_t exposure_us);
extern int camera_set_gain(int gain);
extern int camera_set_brightness(int brightness);
extern int camera_set_contrast(int contrast);
extern int camera_set_saturation(int saturation);
extern int camera_set_white_balance_mode(int mode);
extern int camera_set_trigger_mode(int mode);
extern int camera_set_jpeg_quality(int quality);

#define CAMERA_WIDTH 320
#define CAMERA_HEIGHT 240

extern const char* web_api_get_pixel_format_name(uint32_t pixel_format);

// Camera interface implementation using camera_handler functions
static void esp32_get_camera_status(web_camera_status_t* status) {
    if (!status) return;
    
    status->real_camera = camera_is_real_camera_active();
    status->width = CAMERA_WIDTH;
    status->height = CAMERA_HEIGHT; 
    status->max_payload_size = camera_get_max_payload_size();
    status->pixel_format = camera_get_genicam_pixformat();
    status->pixel_format_name = web_api_get_pixel_format_name(status->pixel_format);
}

static void esp32_get_camera_params(web_camera_params_t* params) {
    if (!params) return;
    
    params->exposure_time = camera_get_exposure_time();
    params->gain = camera_get_gain();
    params->brightness = camera_get_brightness();
    params->contrast = camera_get_contrast();
    params->saturation = camera_get_saturation();
    params->white_balance_mode = camera_get_white_balance_mode();
    params->trigger_mode = camera_get_trigger_mode();
    params->jpeg_quality = camera_get_jpeg_quality();
}

static int esp32_set_camera_param(const char* param_name, int value) {
    if (!param_name) return -1;
    
    platform->log_info(TAG, "Setting camera parameter: %s = %d", param_name, value);
    
    if (strcmp(param_name, "exposure_time") == 0) {
        return camera_set_exposure_time(value);
    } else if (strcmp(param_name, "gain") == 0) {
        return camera_set_gain(value);
    } else if (strcmp(param_name, "brightness") == 0) {
        return camera_set_brightness(value);
    } else if (strcmp(param_name, "contrast") == 0) {
        return camera_set_contrast(value);
    } else if (strcmp(param_name, "saturation") == 0) {
        return camera_set_saturation(value);
    } else if (strcmp(param_name, "white_balance_mode") == 0) {
        return camera_set_white_balance_mode(value);
    } else if (strcmp(param_name, "trigger_mode") == 0) {
        return camera_set_trigger_mode(value);
    } else if (strcmp(param_name, "jpeg_quality") == 0) {
        return camera_set_jpeg_quality(value);
    }
    
    platform->log_warn(TAG, "Unknown camera parameter: %s", param_name);
    return -1;
}

// ESP32 web platform interface
static const web_platform_interface_t esp32_web_platform = {
    .server_start = esp32_server_start,
    .server_stop = esp32_server_stop,
    .register_handler = esp32_register_handler,
    .send_response = esp32_send_response,
    .send_error = esp32_send_error,
    .receive_data = esp32_receive_data,
    .get_request_uri = esp32_get_request_uri,
    .get_content_length = esp32_get_content_length,
    .get_camera_status = esp32_get_camera_status,
    .get_camera_params = esp32_get_camera_params,
    .set_camera_param = esp32_set_camera_param
};

// Platform initialization
void web_platform_init_esp32(void) {
    web_platform = &esp32_web_platform;
}