#pragma once

// Include all modular GVCP components
#include "gvcp_protocol.h"
#include "gvcp_discovery.h"
#include "gvcp_bootstrap.h"
#include "gvcp_registers.h"
#include "gvcp_statistics.h"

// Main GVCP handler functions
esp_err_t gvcp_init(void);
void gvcp_task(void *pvParameters);

// Internal packet handling function
void handle_gvcp_packet(const uint8_t *packet, int len, struct sockaddr_in *client_addr);