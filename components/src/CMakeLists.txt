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
    
    # Utility modules
    "utils/byte_order.c"
    
    # Platform implementation (ESP32)
    "platform_esp32.c"
)

idf_component_register(SRCS ${PLATFORM_INDEPENDENT_SRCS}
                      INCLUDE_DIRS "."
                      REQUIRES esp_timer esp_system lwip
                      PRIV_REQUIRES esp_wifi)