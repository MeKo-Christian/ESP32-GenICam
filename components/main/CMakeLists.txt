# Base source files (ESP32-specific integration layer)
set(COMPONENT_SRCS 
    "main.c"
    "gvcp_handler.c"
    "gvcp_statistics.c"
    "gvcp_registers.c"
    "gvcp_discovery.c"
    "gvcp_bootstrap.c"
    "gvcp_protocol.c"
    "genicam_xml.c"
    "gvsp_handler.c"
    "camera_handler.c" 
    "wifi_manager.c"
    "status_led.c"
)

# Base required components (including new src component)
set(COMPONENT_REQUIRES 
    nvs_flash 
    esp_wifi 
    lwip 
    esp32-camera 
    esp_timer
    esp_http_server
    src
)

# Conditionally add web server
if(CONFIG_ENABLE_WEB_SERVER)
    list(APPEND COMPONENT_SRCS "web_server.c")
endif()

idf_component_register(SRCS ${COMPONENT_SRCS}
                    INCLUDE_DIRS "."
                    REQUIRES ${COMPONENT_REQUIRES}
                    PRIV_REQUIRES json)