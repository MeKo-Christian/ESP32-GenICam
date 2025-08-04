#ifndef WEB_API_H
#define WEB_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Camera status data structure (platform independent)
typedef struct {
    bool real_camera;
    uint32_t width;
    uint32_t height;
    uint32_t max_payload_size;
    uint32_t pixel_format;
    const char* pixel_format_name;
} web_camera_status_t;

// Camera parameters data structure (platform independent)
typedef struct {
    uint32_t exposure_time;
    int gain;
    int brightness;
    int contrast;
    int saturation;
    int white_balance_mode;
    int trigger_mode;
    int jpeg_quality;
} web_camera_params_t;

// JSON API response builders (platform independent)
char* web_api_build_status_json(const web_camera_status_t* status);
char* web_api_build_control_json(const web_camera_params_t* params);
char* web_api_build_success_response(void);
char* web_api_build_error_response(const char* error_message);

// JSON API request parsers (platform independent)
typedef enum {
    WEB_API_PARSE_SUCCESS = 0,
    WEB_API_PARSE_ERROR = -1,
    WEB_API_PARSE_INVALID_JSON = -2,
    WEB_API_PARSE_MISSING_FIELD = -3
} web_api_parse_result_t;

typedef struct {
    const char* param_name;
    int param_value;
    bool param_found;
} web_api_param_update_t;

web_api_parse_result_t web_api_parse_control_request(const char* json_data, 
                                                    web_api_param_update_t* updates, 
                                                    size_t max_updates, 
                                                    size_t* num_updates);

// Helper function to get pixel format name from ID
const char* web_api_get_pixel_format_name(uint32_t pixel_format);

// Memory management (caller must free returned strings)
void web_api_free_json_string(char* json_string);

#endif // WEB_API_H