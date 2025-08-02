#include "gvcp_protocol.h"
#include "gvcp_statistics.h"
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
    nack_header->command = original_header->command;   // Echo back original command
    nack_header->size = htons(GVCP_BYTES_TO_WORDS(2)); // Error code size in 32-bit words (2 bytes = 1 word)
    nack_header->id = original_header->id;             // Echo back packet ID

    // Add error code
    *(uint16_t *)&response[sizeof(gvcp_header_t)] = htons(error_code);

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
        ESP_LOGW(TAG, "Sent NACK response for command 0x%04x with error code 0x%04x",
                 ntohs(original_header->command), error_code);
        return ESP_OK;
    }
}

bool gvcp_validate_packet_header(const gvcp_header_t *header, int packet_len)
{
    if (header == NULL)
        return false;

    // Minimum valid GVCP packet is just the header
    if (packet_len < sizeof(gvcp_header_t))
        return false;

    // GVCP packets must always use the standard packet type (0x42)
    if (header->packet_type != GVCP_PACKET_TYPE)
        return false;

    // Optional strict packet_flags check — allow only 0x00 (CMD) or 0x01 (ACK)
    if ((header->packet_flags & ~GVCP_PACKET_FLAG_ACK) != 0)
        return false;

    // Size field is in 32-bit words; validate it matches actual payload length
    uint16_t payload_size_bytes = ntohs(header->size) * 4;
    if ((size_t)packet_len != sizeof(gvcp_header_t) + payload_size_bytes)
        return false;

    return true;
}

void gvcp_create_response_header(gvcp_header_t *response, const gvcp_header_t *request, uint16_t response_command, uint16_t response_size_words)
{
    if (response == NULL)
        return;

    response->packet_type = GVCP_PACKET_TYPE;
    response->packet_flags = GVCP_PACKET_FLAG_ACK;
    response->command = htons(response_command);
    response->size = htons(response_size_words);

    // Only copy ID if request is present
    if (request)
        response->id = request->id;
}