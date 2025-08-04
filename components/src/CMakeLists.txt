# Platform-independent source files
set(PLATFORM_INDEPENDENT_SRCS
    # GVCP modules
    "gvcp/protocol.c"
    "gvcp/bootstrap.c" 
    "gvcp/discovery.c"
    
    # GVSP modules
    "gvsp/streaming.c"
    "gvsp/packets.c"
    
    # GenICam modules
    "genicam/registers.c"
    "genicam/xml.c"
    
    # Web modules
    "web/web_templates.c"
    "web/web_api.c"
    "web/web_routes.c"
    
    # Utility modules
    "utils/byte_order.c"
    "utils/web_platform.c"
    "utils/web_platform_esp32.c"
    
    # Platform implementation (ESP32)
    "platform_esp32.c"
)

idf_component_register(SRCS ${PLATFORM_INDEPENDENT_SRCS}
                      INCLUDE_DIRS "."
                      REQUIRES esp_timer esp_system lwip esp_http_server
                      PRIV_REQUIRES esp_wifi)