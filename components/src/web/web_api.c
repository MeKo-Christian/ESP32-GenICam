#include "web_api.h"
#include "../utils/platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

// Platform-independent JSON builders
char* web_api_build_status_json(const web_camera_status_t* status) {
    if (!status) return NULL;
    
    // Calculate required buffer size
    size_t buffer_size = 512;
    char* json = platform->malloc(buffer_size);
    if (!json) return NULL;
    
    int written = snprintf(json, buffer_size,
        "{"
        "\"real_camera\":%s,"
        "\"width\":%"PRIu32","
        "\"height\":%"PRIu32","
        "\"max_payload_size\":%"PRIu32","
        "\"pixel_format\":%"PRIu32","
        "\"pixel_format_name\":\"%s\""
        "}",
        status->real_camera ? "true" : "false",
        status->width,
        status->height,
        status->max_payload_size,
        status->pixel_format,
        status->pixel_format_name ? status->pixel_format_name : "Unknown"
    );
    
    if (written >= buffer_size) {
        platform->free(json);
        return NULL;
    }
    
    return json;
}

char* web_api_build_control_json(const web_camera_params_t* params) {
    if (!params) return NULL;
    
    // Calculate required buffer size
    size_t buffer_size = 512;
    char* json = platform->malloc(buffer_size);
    if (!json) return NULL;
    
    int written = snprintf(json, buffer_size,
        "{"
        "\"exposure_time\":%"PRIu32","
        "\"gain\":%d,"
        "\"brightness\":%d,"
        "\"contrast\":%d,"
        "\"saturation\":%d,"
        "\"white_balance_mode\":%d,"
        "\"trigger_mode\":%d,"
        "\"jpeg_quality\":%d"
        "}",
        params->exposure_time,
        params->gain,
        params->brightness,
        params->contrast,
        params->saturation,
        params->white_balance_mode,
        params->trigger_mode,
        params->jpeg_quality
    );
    
    if (written >= buffer_size) {
        platform->free(json);
        return NULL;
    }
    
    return json;
}

char* web_api_build_success_response(void) {
    char* json = platform->malloc(64);
    if (!json) return NULL;
    
    strcpy(json, "{\"success\":true}");
    return json;
}

char* web_api_build_error_response(const char* error_message) {
    if (!error_message) error_message = "Unknown error";
    
    size_t buffer_size = 128 + strlen(error_message);
    char* json = platform->malloc(buffer_size);
    if (!json) return NULL;
    
    int written = snprintf(json, buffer_size,
        "{\"success\":false,\"error\":\"%s\"}",
        error_message
    );
    
    if (written >= buffer_size) {
        platform->free(json);
        return NULL;
    }
    
    return json;
}

// Simple JSON parser for control requests (minimal implementation)
web_api_parse_result_t web_api_parse_control_request(const char* json_data, 
                                                    web_api_param_update_t* updates, 
                                                    size_t max_updates, 
                                                    size_t* num_updates) {
    if (!json_data || !updates || !num_updates) {
        return WEB_API_PARSE_ERROR;
    }
    
    *num_updates = 0;
    
    // Simple parameter extraction - look for known parameter patterns
    const char* param_names[] = {
        "exposure_time", "gain", "brightness", "contrast", 
        "saturation", "white_balance_mode", "trigger_mode", "jpeg_quality"
    };
    
    for (size_t i = 0; i < sizeof(param_names)/sizeof(param_names[0]) && *num_updates < max_updates; i++) {
        char search_pattern[64];
        snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", param_names[i]);
        
        const char* param_pos = strstr(json_data, search_pattern);
        if (param_pos) {
            param_pos += strlen(search_pattern);
            
            // Skip whitespace
            while (*param_pos == ' ' || *param_pos == '\t') param_pos++;
            
            // Parse integer value
            char* end_ptr;
            long value = strtol(param_pos, &end_ptr, 10);
            
            if (end_ptr != param_pos) {  // Successfully parsed a number
                updates[*num_updates].param_name = param_names[i];
                updates[*num_updates].param_value = (int)value;
                updates[*num_updates].param_found = true;
                (*num_updates)++;
            }
        }
    }
    
    return (*num_updates > 0) ? WEB_API_PARSE_SUCCESS : WEB_API_PARSE_MISSING_FIELD;
}

// Helper function to get pixel format name from ID
const char* web_api_get_pixel_format_name(uint32_t pixel_format) {
    switch (pixel_format) {
        case 0x01080001: return "Mono8";
        case 0x02100005: return "RGB565Packed";
        case 0x02100004: return "YUV422Packed";
        case 0x02180014: return "RGB8Packed";
        case 0x80000001: return "JPEG";
        default: return "Unknown";
    }
}

// Memory management
void web_api_free_json_string(char* json_string) {
    if (json_string) {
        platform->free(json_string);
    }
}