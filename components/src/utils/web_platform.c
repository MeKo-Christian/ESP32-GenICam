#include "web_platform.h"
#include "../web/web_templates.h"
#include "../web/web_api.h"
#include "../web/web_routes.h"
#include "platform.h"
#include <string.h>

// Global web platform interface pointer
const web_platform_interface_t* web_platform = NULL;

// Platform-independent request handler implementations
int web_handle_interface_request(web_request_handle_t req) {
    if (!web_platform || !web_platform->send_response) {
        return -1;
    }
    
    const char* html = web_get_interface_html();
    size_t html_size = web_get_interface_html_size();
    
    return web_platform->send_response(req, "text/html", html, html_size);
}

int web_handle_status_request(web_request_handle_t req) {
    if (!web_platform || !web_platform->get_camera_status || !web_platform->send_response) {
        return -1;
    }
    
    web_camera_status_t status;
    web_platform->get_camera_status(&status);
    
    char* json = web_api_build_status_json(&status);
    if (!json) {
        return web_platform->send_error(req, 500);
    }
    
    int result = web_platform->send_response(req, "application/json", json, strlen(json));
    web_api_free_json_string(json);
    
    return result;
}

int web_handle_control_get_request(web_request_handle_t req) {
    if (!web_platform || !web_platform->get_camera_params || !web_platform->send_response) {
        return -1;
    }
    
    web_camera_params_t params;
    web_platform->get_camera_params(&params);
    
    char* json = web_api_build_control_json(&params);
    if (!json) {
        return web_platform->send_error(req, 500);
    }
    
    int result = web_platform->send_response(req, "application/json", json, strlen(json));
    web_api_free_json_string(json);
    
    return result;
}

int web_handle_control_post_request(web_request_handle_t req) {
    if (!web_platform || !web_platform->set_camera_param || !web_platform->send_response) {
        return -1;
    }
    
    // Get content length
    size_t content_len = web_platform->get_content_length(req);
    if (content_len == 0 || content_len > 1024) {
        return web_platform->send_error(req, 400);
    }
    
    // Receive JSON data
    char* buffer = platform->malloc(content_len + 1);
    if (!buffer) {
        return web_platform->send_error(req, 500);
    }
    
    int received = web_platform->receive_data(req, buffer, content_len);
    if (received <= 0) {
        platform->free(buffer);
        return web_platform->send_error(req, 400);
    }
    buffer[received] = '\0';
    
    // Parse parameter updates
    web_api_param_update_t updates[8];
    size_t num_updates;
    
    web_api_parse_result_t parse_result = web_api_parse_control_request(buffer, updates, 8, &num_updates);
    platform->free(buffer);
    
    if (parse_result != WEB_API_PARSE_SUCCESS) {
        char* error_json = web_api_build_error_response("Invalid JSON data");
        if (error_json) {
            int result = web_platform->send_response(req, "application/json", error_json, strlen(error_json));
            web_api_free_json_string(error_json);
            return result;
        }
        return web_platform->send_error(req, 400);
    }
    
    // Apply parameter updates
    bool success = true;
    for (size_t i = 0; i < num_updates; i++) {
        if (updates[i].param_found) {
            int result = web_platform->set_camera_param(updates[i].param_name, updates[i].param_value);
            if (result != 0) {
                success = false;
                break;
            }
        }
    }
    
    // Send response
    char* response_json = success ? web_api_build_success_response() : 
                                  web_api_build_error_response("Failed to update parameters");
    
    if (!response_json) {
        return web_platform->send_error(req, 500);
    }
    
    int result = web_platform->send_response(req, "application/json", response_json, strlen(response_json));
    web_api_free_json_string(response_json);
    
    return result;
}