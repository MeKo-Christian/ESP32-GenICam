#include "protocol.h"
#include "statistics.h"
#include "esp_log.h"
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include "lwip/sockets.h"

static const char *TAG = "gvcp_protocol";

// External socket reference (maintained in gvcp_handler.c)
extern int sock;

// Socket error tracking
static uint32_t gvcp_socket_error_count = 0;
static uint32_t gvcp_max_socket_errors = 3;

// Helper function for GVCP sendto with error handling
esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr)
{
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Invalid GVCP socket for transmission");
        gvcp_socket_error_count++;
        return ESP_FAIL;
    }

    if (data == NULL || data_len == 0 || client_addr == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for GVCP sendto");
        return ESP_ERR_INVALID_ARG;
    }

    int bytes_sent = sendto(sock, data, data_len, 0, (struct sockaddr *)client_addr, sizeof(*client_addr));

    if (bytes_sent < 0)
    {
        ESP_LOGE(TAG, "GVCP sendto failed: errno %d (%s)", errno, strerror(errno));
        gvcp_socket_error_count++;

        // Check if we should recreate socket due to persistent errors
        if (gvcp_socket_error_count >= gvcp_max_socket_errors)
        {
            ESP_LOGW(TAG, "GVCP socket error count reached %d, considering recreation", gvcp_socket_error_count);
        }

        return ESP_FAIL;
    }
    else if (bytes_sent != (int)data_len)
    {
        ESP_LOGW(TAG, "GVCP sendto partial transmission: %d/%d bytes", bytes_sent, data_len);
        return ESP_FAIL;
    }
    else
    {
        // Reset error count on successful transmission
        gvcp_socket_error_count = 0;
        return ESP_OK;
    }
}

uint16_t gvcp_get_ack_command(uint16_t cmd_command)
{
    // Convert network byte order to host byte order for comparison
    uint16_t host_cmd = ntohs(cmd_command);

    switch (host_cmd)
    {
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
        ESP_LOGW(TAG, "Unknown command 0x%04x, using original in NACK", host_cmd);
        return cmd_command;
    }
}

esp_err_t gvcp_send_nack(const gvcp_header_t *original_header, uint16_t error_code, struct sockaddr_in *client_addr)
{
    if (original_header == NULL || client_addr == NULL)
    {
        return ESP_ERR_INVALID_ARG;
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
    ESP_LOGW(TAG, "NACK packet: type=0x%02x (ERROR), orig_cmd=0x%04x, ack_cmd=0x%04x, error_code=0x%04x",
             nack_header->packet_type, ntohs(original_header->command), ntohs(nack_header->command), error_code);

    // Send NACK response
    esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error sending NACK response");
        return ESP_FAIL;
    }
    else
    {
        gvcp_increment_total_errors();
        ESP_LOGW(TAG, "Successfully sent NACK response for command 0x%04x→0x%04x with error code 0x%04x",
                 ntohs(original_header->command), ntohs(nack_header->command), error_code);
        return ESP_OK;
    }
}

bool gvcp_validate_packet_header(const gvcp_header_t *header, int packet_len)
{
    if (header == NULL)
        return false;

    if (packet_len < sizeof(gvcp_header_t))
        return false;

    // Allow known packet types
    switch (header->packet_type)
    {
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

void gvcp_create_command_header(gvcp_header_t *cmd, uint16_t command_code, uint16_t size_words, uint16_t packet_id, bool ack_required)
{
    if (!cmd)
        return;

    cmd->packet_type = GVCP_PACKET_TYPE_CMD;
    cmd->packet_flags = ack_required ? GVCP_FLAGS_ACK_REQUIRED : 0x00;
    cmd->command = htons(command_code);
    cmd->size = htons(size_words);
    cmd->id = htons(packet_id);
}

void gvcp_create_ack_header(gvcp_header_t *ack, const gvcp_header_t *request, uint16_t ack_code, uint16_t size_words)
{
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