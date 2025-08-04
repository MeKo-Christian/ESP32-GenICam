#include "discovery.h"
#include "protocol.h"
#include "../utils/platform.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "gvcp_discovery";

// Discovery broadcast configuration
static bool discovery_broadcast_enabled = false;
static uint32_t discovery_broadcast_interval_ms = 5000;
static uint32_t last_discovery_broadcast_time = 0;
static uint32_t discovery_broadcast_sequence = 0;
static uint32_t discovery_broadcast_retries = 3;
static uint32_t discovery_broadcasts_sent = 0;
static uint32_t discovery_broadcast_failures = 0;

// Callback functions
static gvcp_discovery_get_bootstrap_callback_t get_bootstrap_callback = NULL;
static gvcp_discovery_set_gvsp_client_callback_t set_gvsp_client_callback = NULL;
static gvcp_discovery_set_connection_status_callback_t set_connection_status_callback = NULL;

// GVCP connection status bits
#define GVCP_CONNECTION_STATUS_CLIENT_CONN 0x01

void gvcp_discovery_set_bootstrap_callback(gvcp_discovery_get_bootstrap_callback_t callback) {
    get_bootstrap_callback = callback;
}

void gvcp_discovery_set_gvsp_client_callback(gvcp_discovery_set_gvsp_client_callback_t callback) {
    set_gvsp_client_callback = callback;
}

void gvcp_discovery_set_connection_status_callback(gvcp_discovery_set_connection_status_callback_t callback) {
    set_connection_status_callback = callback;
}

// Internal function to handle discovery response with flexible header format
static gvcp_discovery_result_t send_discovery_internal(uint16_t packet_id, void *dest_addr, bool use_structured_header) {
    if (get_bootstrap_callback == NULL) {
        platform->log_error(TAG, "Bootstrap callback not set");
        return GVCP_DISCOVERY_ERROR;
    }

    if (use_structured_header) {
        // GigE Vision specification compliant structured header format
        uint8_t response[sizeof(gvcp_header_t) + GVBS_DISCOVERY_DATA_SIZE];
        gvcp_header_t *ack_header = (gvcp_header_t *)response;

        uint16_t discovery_data_size_in_words = GVBS_DISCOVERY_DATA_SIZE / 4;
        gvcp_create_ack_header(ack_header, NULL, GVCP_ACK_DISCOVERY, discovery_data_size_in_words);
        ack_header->id = packet_id;

        // Copy bootstrap data
        uint8_t *bootstrap_memory = get_bootstrap_callback();
        memcpy(&response[sizeof(gvcp_header_t)], bootstrap_memory, GVBS_DISCOVERY_DATA_SIZE);

        platform->log_info(TAG, "GigE Vision SPEC: Sending discovery response with packet ID=0x%04x", ntohs(packet_id));

        // Send discovery response directly using callback (not as NACK since this is an ACK response)
        gvcp_result_t result = gvcp_send_response(response, sizeof(response), dest_addr);
        
        if (result == GVCP_RESULT_SUCCESS) {
            platform->log_info(TAG, "GigE Vision SPEC: Discovery response sent (%d bytes)", sizeof(response));

            // Set GVSP client address for streaming
            if (set_gvsp_client_callback) {
                set_gvsp_client_callback(dest_addr);
            }
            
            // Set client connected bit
            if (set_connection_status_callback) {
                set_connection_status_callback(GVCP_CONNECTION_STATUS_CLIENT_CONN, true);
            }
            
            return GVCP_DISCOVERY_SUCCESS;
        } else {
            platform->log_warn(TAG, "Discovery response send failed");
            return GVCP_DISCOVERY_SEND_FAILED;
        }
    } else {
        // Raw header format (8-byte GigE Vision header + bootstrap data)
        // Used for broadcast compatibility with legacy clients
        uint8_t response[8 + GVBS_DISCOVERY_DATA_SIZE];

        // Construct GigE Vision GVCP header with magic bytes 0x42 0x45
        response[0] = 0x42;                             // 'B' - GigE Vision magic byte 1
        response[1] = 0x45;                             // 'E' - GigE Vision magic byte 2
        response[2] = GVCP_PACKET_TYPE_ACK;             // 0x00
        response[3] = GVCP_PACKET_FLAG_ACK;             // 0x01
        response[4] = (GVCP_ACK_DISCOVERY >> 8) & 0xFF; // Command high byte
        response[5] = GVCP_ACK_DISCOVERY & 0xFF;        // Command low byte
        response[6] = (packet_id >> 8) & 0xFF;          // Packet ID high byte
        response[7] = packet_id & 0xFF;                 // Packet ID low byte

        // Copy bootstrap memory as discovery payload
        uint8_t *bootstrap_memory = get_bootstrap_callback();
        memcpy(&response[8], bootstrap_memory, GVBS_DISCOVERY_DATA_SIZE);

        platform->log_info(TAG, "Sending discovery response (ID: 0x%04x, raw format)", ntohs(packet_id));

        // Send using platform network interface
        int result = platform->network_send(response, sizeof(response), dest_addr);
        
        if (result >= 0) {
            return GVCP_DISCOVERY_SUCCESS;
        } else {
            return GVCP_DISCOVERY_SEND_FAILED;
        }
    }
}

gvcp_discovery_result_t gvcp_discovery_send_gige_spec_response(uint16_t packet_id, void *dest_addr) {
    return send_discovery_internal(packet_id, dest_addr, true);
}

gvcp_discovery_result_t gvcp_discovery_send_response(const gvcp_header_t *request_header, void *dest_addr, bool use_structured_header) {
    uint16_t packet_id;
    if (request_header != NULL) {
        // Solicited response - echo back the exact packet ID
        packet_id = request_header->id;
        platform->log_info(TAG, "SOLICITED Response: echoing back packet ID=0x%04x", ntohs(packet_id));
    } else {
        // Unsolicited broadcast - use current sequence number (incremented per packet)
        packet_id = htons(discovery_broadcast_sequence & 0xFFFF);
        platform->log_info(TAG, "UNSOLICITED Broadcast: using sequence=%d as unique packet ID=0x%04x",
                 discovery_broadcast_sequence, ntohs(packet_id));
    }

    return send_discovery_internal(packet_id, dest_addr, use_structured_header);
}

gvcp_discovery_result_t gvcp_discovery_handle_command(const gvcp_header_t *header, void *client_addr) {
    uint16_t request_id = ntohs(header->id);
    platform->log_info(TAG, "Discovery SOLICITED request ID:0x%04x - MUST echo back exactly", request_id);

    // Send GigE Vision spec compliant response
    gvcp_discovery_result_t result = gvcp_discovery_send_gige_spec_response(header->id, client_addr);
    
    if (result == GVCP_DISCOVERY_SUCCESS) {
        platform->log_info(TAG, "Discovery response sent successfully");
    } else {
        platform->log_error(TAG, "Discovery response failed");
    }
    
    return result;
}

gvcp_discovery_result_t gvcp_discovery_send_broadcast(void) {
    if (!discovery_broadcast_enabled) {
        platform->log_debug(TAG, "Discovery broadcast disabled");
        return GVCP_DISCOVERY_SUCCESS;
    }

    // Increment sequence for unique packet ID
    discovery_broadcast_sequence++;
    
    // For broadcast, we would need a broadcast address
    // This is platform-specific, so for now we just log
    platform->log_info(TAG, "Discovery broadcast triggered (sequence: %d)", discovery_broadcast_sequence);
    
    // Update statistics
    discovery_broadcasts_sent++;
    last_discovery_broadcast_time = platform->get_time_ms();
    
    return GVCP_DISCOVERY_SUCCESS;
}

void gvcp_discovery_enable_broadcast(bool enable) {
    discovery_broadcast_enabled = enable;
    platform->log_info(TAG, "Discovery broadcast %s", enable ? "enabled" : "disabled");
}

void gvcp_discovery_set_broadcast_interval(uint32_t interval_ms) {
    discovery_broadcast_interval_ms = interval_ms;
    platform->log_info(TAG, "Discovery broadcast interval set to %d ms", interval_ms);
}

gvcp_discovery_result_t gvcp_discovery_trigger_broadcast(void) {
    return gvcp_discovery_send_broadcast();
}

void gvcp_discovery_process_periodic(void) {
    if (!discovery_broadcast_enabled) {
        return;
    }

    uint32_t current_time = platform->get_time_ms();
    if (current_time - last_discovery_broadcast_time >= discovery_broadcast_interval_ms) {
        gvcp_discovery_send_broadcast();
    }
}

void gvcp_discovery_get_config(gvcp_discovery_config_t *config) {
    if (config) {
        config->enabled = discovery_broadcast_enabled;
        config->interval_ms = discovery_broadcast_interval_ms;
        config->retries = discovery_broadcast_retries;
    }
}

void gvcp_discovery_get_stats(gvcp_discovery_stats_t *stats) {
    if (stats) {
        stats->broadcasts_sent = discovery_broadcasts_sent;
        stats->broadcast_failures = discovery_broadcast_failures;
        stats->sequence_number = discovery_broadcast_sequence;
        stats->last_broadcast_time_ms = last_discovery_broadcast_time;
    }
}

gvcp_discovery_result_t gvcp_discovery_init(void) {
    // Initialize discovery configuration
    discovery_broadcast_enabled = false;
    discovery_broadcast_interval_ms = 5000;
    last_discovery_broadcast_time = 0;
    discovery_broadcast_sequence = 0;
    discovery_broadcast_retries = 3;
    discovery_broadcasts_sent = 0;
    discovery_broadcast_failures = 0;

    platform->log_info(TAG, "Discovery module initialized");
    return GVCP_DISCOVERY_SUCCESS;
}