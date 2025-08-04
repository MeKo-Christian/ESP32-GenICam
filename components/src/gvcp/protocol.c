#include "protocol.h"
#include "../utils/platform.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "gvcp_protocol";

// Network send callback
static gvcp_send_callback_t send_callback = NULL;

// Socket error tracking
static uint32_t gvcp_socket_error_count = 0;
static uint32_t gvcp_max_socket_errors = 3;

void gvcp_set_send_callback(gvcp_send_callback_t callback) {
    send_callback = callback;
}

// Helper function for GVCP sendto with error handling
static gvcp_result_t gvcp_sendto(const void *data, size_t data_len, void *client_addr) {
    if (send_callback == NULL) {
        platform->log_error(TAG, "No send callback set for GVCP transmission");
        gvcp_socket_error_count++;
        return GVCP_RESULT_ERROR;
    }

    if (data == NULL || data_len == 0 || client_addr == NULL) {
        platform->log_error(TAG, "Invalid parameters for GVCP sendto");
        return GVCP_RESULT_INVALID_ARG;
    }

    gvcp_result_t result = send_callback(data, data_len, client_addr);

    if (result != GVCP_RESULT_SUCCESS) {
        platform->log_error(TAG, "GVCP sendto failed");
        gvcp_socket_error_count++;

        // Check if we should recreate socket due to persistent errors
        if (gvcp_socket_error_count >= gvcp_max_socket_errors) {
            platform->log_warn(TAG, "GVCP socket error count reached %d, considering recreation", gvcp_socket_error_count);
        }

        return GVCP_RESULT_SEND_FAILED;
    } else {
        // Reset error count on successful transmission
        gvcp_socket_error_count = 0;
        return GVCP_RESULT_SUCCESS;
    }
}

uint16_t gvcp_get_ack_command(uint16_t cmd_command) {
    // Convert network byte order to host byte order for comparison
    uint16_t host_cmd = ntohs(cmd_command);

    switch (host_cmd) {
    case GVCP_CMD_DISCOVERY:
        return htons(GVCP_ACK_DISCOVERY);
    case GVCP_CMD_PACKETRESEND:
        return htons(GVCP_ACK_PACKETRESEND);
    case GVCP_CMD_READREG:
        return htons(GVCP_ACK_READREG);
    case GVCP_CMD_WRITEREG:
        return htons(GVCP_ACK_WRITEREG);
    case GVCP_CMD_READ_MEMORY:
        return htons(GVCP_ACK_READ_MEMORY);
    case GVCP_CMD_WRITE_MEMORY:
        return htons(GVCP_ACK_WRITE_MEMORY);
    default:
        // For unknown commands, return the original command
        // This maintains backward compatibility
        platform->log_warn(TAG, "Unknown command 0x%04x, using original in NACK", host_cmd);
        return cmd_command;
    }
}

gvcp_result_t gvcp_send_nack(const gvcp_header_t *original_header, uint16_t error_code, void *client_addr) {
    if (original_header == NULL || client_addr == NULL) {
        return GVCP_RESULT_INVALID_ARG;
    }

    uint8_t response[sizeof(gvcp_header_t) + 2];
    gvcp_header_t *nack_header = (gvcp_header_t *)response;

    // Create NACK response header
    nack_header->packet_type = GVCP_PACKET_TYPE_ERROR;
    nack_header->packet_flags = 0;
    nack_header->command = gvcp_get_ack_command(original_header->command); // Use corresponding ACK command
    nack_header->size = htons(2);                                          // Error code size (2 bytes)
    nack_header->id = original_header->id;                                 // Echo back packet ID

    // Add error code
    *(uint16_t *)&response[sizeof(gvcp_header_t)] = htons(error_code);

    // Log NACK packet details before sending
    platform->log_warn(TAG, "NACK packet: type=0x%02x (ERROR), orig_cmd=0x%04x, ack_cmd=0x%04x, error_code=0x%04x",
             nack_header->packet_type, ntohs(original_header->command), ntohs(nack_header->command), error_code);

    // Send NACK response
    gvcp_result_t result = gvcp_sendto(response, sizeof(response), client_addr);

    if (result != GVCP_RESULT_SUCCESS) {
        platform->log_error(TAG, "Error sending NACK response");
        return GVCP_RESULT_SEND_FAILED;
    } else {
        platform->log_warn(TAG, "Successfully sent NACK response for command 0x%04x→0x%04x with error code 0x%04x",
                 ntohs(original_header->command), ntohs(nack_header->command), error_code);
        return GVCP_RESULT_SUCCESS;
    }
}

// Send a GVCP response packet directly (for ACK responses like discovery)
gvcp_result_t gvcp_send_response(const void *data, size_t data_len, void *client_addr) {
    if (data == NULL || data_len == 0 || client_addr == NULL) {
        platform->log_error(TAG, "Invalid parameters for GVCP response send");
        return GVCP_RESULT_INVALID_ARG;
    }

    // Send response directly using the callback
    gvcp_result_t result = gvcp_sendto(data, data_len, client_addr);
    
    if (result != GVCP_RESULT_SUCCESS) {
        platform->log_error(TAG, "Error sending GVCP response");
        return GVCP_RESULT_SEND_FAILED;
    } else {
        platform->log_info(TAG, "Successfully sent GVCP response (%zu bytes)", data_len);
        return GVCP_RESULT_SUCCESS;
    }
}

bool gvcp_validate_packet_header(const gvcp_header_t *header, int packet_len) {
    if (header == NULL)
        return false;

    if (packet_len < sizeof(gvcp_header_t))
        return false;

    // Allow known packet types
    switch (header->packet_type) {
    case 0x42: // Command
    case 0x00: // ACK
    case 0x80: // NACK/Error
        break;
    default:
        return false;
    }

    uint16_t payload_size_bytes = ntohs(header->size) * 4;
    if ((size_t)packet_len != sizeof(gvcp_header_t) + payload_size_bytes)
        return false;

    return true;
}

void gvcp_create_command_header(gvcp_header_t *cmd, uint16_t command_code, uint16_t size_words, uint16_t packet_id, bool ack_required) {
    if (!cmd)
        return;

    cmd->packet_type = GVCP_PACKET_TYPE_CMD;
    cmd->packet_flags = ack_required ? GVCP_FLAGS_ACK_REQUIRED : 0x00;
    cmd->command = htons(command_code);
    cmd->size = htons(size_words);
    cmd->id = htons(packet_id);
}

void gvcp_create_ack_header(gvcp_header_t *ack, const gvcp_header_t *request, uint16_t ack_code, uint16_t size_words) {
    if (!ack)
        return;

    ack->packet_type = GVCP_PACKET_TYPE_ACK;
    ack->packet_flags = 0x00;
    ack->command = htons(ack_code);
    ack->size = htons(size_words);

    if (request)
        ack->id = request->id;
    else
        ack->id = 0; // fallback; optional
}