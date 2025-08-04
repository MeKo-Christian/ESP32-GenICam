#pragma once

#include <sys/socket.h>
#include <netinet/in.h>

// Include abstracted GVCP components
#include "gvcp/protocol.h"
#include "gvcp/discovery.h"
#include "gvcp/bootstrap.h"
#include "genicam/registers.h"
#include "gvcp_statistics.h"

// Protocol debug logging macros
#ifdef CONFIG_ENABLE_PROTOCOL_DEBUG_LOGS
    #include "esp_log.h"
    #define PROTOCOL_LOG_I(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
    #define PROTOCOL_LOG_W(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
    #define PROTOCOL_LOG_D(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
    #define PROTOCOL_LOG_BUFFER_HEX(tag, buffer, size, level) ESP_LOG_BUFFER_HEX_LEVEL(tag, buffer, size, level)
#else
    #define PROTOCOL_LOG_I(tag, format, ...)
    #define PROTOCOL_LOG_W(tag, format, ...)
    #define PROTOCOL_LOG_D(tag, format, ...)
    #define PROTOCOL_LOG_BUFFER_HEX(tag, buffer, size, level)
#endif

// Main GVCP handler functions
esp_err_t gvcp_init(void);
void gvcp_task(void *pvParameters);

// Internal packet handling function
void handle_gvcp_packet(const uint8_t *packet, int len, struct sockaddr_in *client_addr);