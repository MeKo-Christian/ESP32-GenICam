#include "web_routes.h"
#include <string.h>

// Route configuration table
typedef struct {
    web_route_id_t id;
    const char* uri;
    const char* content_type;
    bool allow_get;
    bool allow_post;
} web_route_config_t;

static const web_route_config_t route_table[] = {
    { WEB_ROUTE_ROOT, "/", "text/html", true, false },
    { WEB_ROUTE_CAMERA_STATUS, "/api/camera/status", "application/json", true, false },
    { WEB_ROUTE_CAMERA_CONTROL, "/api/camera/control", "application/json", true, true },
};

static const size_t route_table_size = sizeof(route_table) / sizeof(route_table[0]);

// Route matching implementation
web_route_id_t web_routes_match_uri(const char* uri) {
    if (!uri) return WEB_ROUTE_UNKNOWN;
    
    for (size_t i = 0; i < route_table_size; i++) {
        if (strcmp(uri, route_table[i].uri) == 0) {
            return route_table[i].id;
        }
    }
    
    return WEB_ROUTE_UNKNOWN;
}

web_method_t web_routes_parse_method(const char* method_str) {
    if (!method_str) return WEB_METHOD_UNKNOWN;
    
    if (strcmp(method_str, "GET") == 0) return WEB_METHOD_GET;
    if (strcmp(method_str, "POST") == 0) return WEB_METHOD_POST;
    if (strcmp(method_str, "PUT") == 0) return WEB_METHOD_PUT;
    if (strcmp(method_str, "DELETE") == 0) return WEB_METHOD_DELETE;
    
    return WEB_METHOD_UNKNOWN;
}

const char* web_routes_get_content_type(web_route_id_t route) {
    for (size_t i = 0; i < route_table_size; i++) {
        if (route_table[i].id == route) {
            return route_table[i].content_type;
        }
    }
    return "text/plain";
}

bool web_routes_is_method_allowed(web_route_id_t route, web_method_t method) {
    for (size_t i = 0; i < route_table_size; i++) {
        if (route_table[i].id == route) {
            switch (method) {
                case WEB_METHOD_GET:
                    return route_table[i].allow_get;
                case WEB_METHOD_POST:
                    return route_table[i].allow_post;
                default:
                    return false;
            }
        }
    }
    return false;
}

const char* web_routes_get_route_uri(web_route_id_t route) {
    for (size_t i = 0; i < route_table_size; i++) {
        if (route_table[i].id == route) {
            return route_table[i].uri;
        }
    }
    return NULL;
}

const char* web_routes_get_method_string(web_method_t method) {
    switch (method) {
        case WEB_METHOD_GET: return "GET";
        case WEB_METHOD_POST: return "POST";
        case WEB_METHOD_PUT: return "PUT";
        case WEB_METHOD_DELETE: return "DELETE";
        default: return "UNKNOWN";
    }
}