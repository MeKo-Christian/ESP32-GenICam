#ifndef WEB_ROUTES_H
#define WEB_ROUTES_H

#include <stdbool.h>

// Route definitions (platform independent)
typedef enum {
    WEB_ROUTE_UNKNOWN = 0,
    WEB_ROUTE_ROOT,              // "/"
    WEB_ROUTE_CAMERA_STATUS,     // "/api/camera/status"
    WEB_ROUTE_CAMERA_CONTROL,    // "/api/camera/control"
} web_route_id_t;

typedef enum {
    WEB_METHOD_GET = 0,
    WEB_METHOD_POST,
    WEB_METHOD_PUT,
    WEB_METHOD_DELETE,
    WEB_METHOD_UNKNOWN
} web_method_t;

// Route matching functions (platform independent)
web_route_id_t web_routes_match_uri(const char* uri);
web_method_t web_routes_parse_method(const char* method_str);
const char* web_routes_get_content_type(web_route_id_t route);
bool web_routes_is_method_allowed(web_route_id_t route, web_method_t method);

// Route information getters
const char* web_routes_get_route_uri(web_route_id_t route);
const char* web_routes_get_method_string(web_method_t method);

#endif // WEB_ROUTES_H