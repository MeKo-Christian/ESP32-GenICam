#ifndef GVCP_DISCOVERY_H
#define GVCP_DISCOVERY_H

#include "protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GVBS_DISCOVERY_DATA_SIZE 0xF8

// Discovery result codes
typedef enum {
    GVCP_DISCOVERY_SUCCESS = 0,
    GVCP_DISCOVERY_ERROR = -1,
    GVCP_DISCOVERY_INVALID_ARG = -2,
    GVCP_DISCOVERY_SEND_FAILED = -3
} gvcp_discovery_result_t;

// Discovery broadcast configuration
typedef struct {
    bool enabled;
    uint32_t interval_ms;
    uint32_t retries;
} gvcp_discovery_config_t;

// Discovery statistics
typedef struct {
    uint32_t broadcasts_sent;
    uint32_t broadcast_failures;
    uint32_t sequence_number;
    uint32_t last_broadcast_time_ms;
} gvcp_discovery_stats_t;

// Discovery packet handling
gvcp_discovery_result_t gvcp_discovery_handle_command(const gvcp_header_t *header, void *client_addr);
gvcp_discovery_result_t gvcp_discovery_send_response(const gvcp_header_t *request_header, void *dest_addr, bool use_structured_header);
gvcp_discovery_result_t gvcp_discovery_send_gige_spec_response(uint16_t packet_id, void *dest_addr);
gvcp_discovery_result_t gvcp_discovery_send_broadcast(void);

// Discovery broadcast management
void gvcp_discovery_enable_broadcast(bool enable);
void gvcp_discovery_set_broadcast_interval(uint32_t interval_ms);
gvcp_discovery_result_t gvcp_discovery_trigger_broadcast(void);
void gvcp_discovery_process_periodic(void);

// Discovery configuration and statistics
void gvcp_discovery_get_config(gvcp_discovery_config_t *config);
void gvcp_discovery_get_stats(gvcp_discovery_stats_t *stats);

// Discovery initialization
gvcp_discovery_result_t gvcp_discovery_init(void);

// Bootstrap memory callback function type (to get discovery data)
typedef uint8_t* (*gvcp_discovery_get_bootstrap_callback_t)(void);

// Set the bootstrap memory callback (must be called before using discovery functions)
void gvcp_discovery_set_bootstrap_callback(gvcp_discovery_get_bootstrap_callback_t callback);

// GVSP client address callback function type
typedef void (*gvcp_discovery_set_gvsp_client_callback_t)(void *client_addr);

// Set the GVSP client address callback (called when discovery response is sent)
void gvcp_discovery_set_gvsp_client_callback(gvcp_discovery_set_gvsp_client_callback_t callback);

// Connection status callback function type
typedef void (*gvcp_discovery_set_connection_status_callback_t)(uint8_t status_bit, bool value);

// Set the connection status callback (called when client connection is established)
void gvcp_discovery_set_connection_status_callback(gvcp_discovery_set_connection_status_callback_t callback);

#endif // GVCP_DISCOVERY_H