#pragma once

#include "protocol.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define GVBS_DISCOVERY_DATA_SIZE 0xF8

// Discovery broadcast management
void gvcp_enable_discovery_broadcast(bool enable);
void gvcp_set_discovery_broadcast_interval(uint32_t interval_ms);
esp_err_t gvcp_trigger_discovery_broadcast(void);
uint32_t gvcp_get_discovery_broadcasts_sent(void);
uint32_t gvcp_get_discovery_broadcast_failures(void);
uint32_t gvcp_get_discovery_broadcast_sequence(void);

// Discovery packet handling
void handle_discovery_cmd(const gvcp_header_t *header, struct sockaddr_in *client_addr);
esp_err_t send_discovery_response(const gvcp_header_t *request_header, struct sockaddr_in *dest_addr, bool is_broadcast);
esp_err_t send_gige_spec_discovery_response(uint16_t exact_packet_id, struct sockaddr_in *dest_addr);
esp_err_t send_discovery_broadcast(void);

// Device identification
void generate_device_uuid(uint8_t *uuid_out, const uint8_t *mac, const char *serial_number);
uint32_t simple_hash(const uint8_t *data, size_t len, uint32_t seed);

// Discovery initialization
esp_err_t gvcp_discovery_init(void);
void gvcp_discovery_process_periodic(void);