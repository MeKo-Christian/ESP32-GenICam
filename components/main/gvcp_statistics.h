#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Error statistics functions
uint32_t gvcp_get_total_commands_received(void);
uint32_t gvcp_get_total_errors_sent(void);
uint32_t gvcp_get_total_unknown_commands(void);

// Error statistics tracking
void gvcp_increment_total_commands(void);
void gvcp_increment_total_errors(void);
void gvcp_increment_unknown_commands(void);

// Connection status management
void gvcp_set_connection_status_bit(uint8_t bit_position, bool value);
uint32_t gvcp_get_connection_status(void);

// Socket health monitoring
uint32_t gvcp_get_socket_error_count(void);
void gvcp_increment_socket_error_count(void);
void gvcp_reset_socket_error_count(void);
bool gvcp_should_recreate_socket(void);
void gvcp_update_socket_recreation_time(void);

// Connection status bit definitions
// Bit 0: GVCP socket active
// Bit 1: GVSP socket active
// Bit 2: Client connected
// Bit 3: Streaming active
#define GVCP_CONNECTION_STATUS_GVCP_SOCKET 0
#define GVCP_CONNECTION_STATUS_GVSP_SOCKET 1
#define GVCP_CONNECTION_STATUS_CLIENT_CONN 2
#define GVCP_CONNECTION_STATUS_STREAMING 3

// Statistics initialization
esp_err_t gvcp_statistics_init(void);
void gvcp_statistics_reset(void);