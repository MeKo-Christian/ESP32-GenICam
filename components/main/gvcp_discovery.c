#include "gvcp_discovery.h"
#include "gvcp_bootstrap.h"
#include "gvcp_protocol.h"
#include "gvcp_statistics.h"
#include "gvcp_handler.h"
#include "gvsp_handler.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "lwip/sockets.h"
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

static const char *TAG = "gvcp_discovery";

// External socket reference (maintained in gvcp_handler.c)
extern int sock;

// Discovery broadcast configuration
static bool discovery_broadcast_enabled = false;
static uint32_t discovery_broadcast_interval_ms = 5000;
static uint32_t last_discovery_broadcast_time = 0;
static uint32_t discovery_broadcast_sequence = 0;
static uint32_t discovery_broadcast_retries = 3; // Number of retries for broadcast
static uint32_t discovery_broadcasts_sent = 0;
static uint32_t discovery_broadcast_failures = 0;

// Forward declaration of internal sendto function (will need to be refactored)
extern esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);

// Simple hash function for UUID generation
uint32_t simple_hash(const uint8_t *data, size_t len, uint32_t seed)
{
    uint32_t hash = seed;
    for (size_t i = 0; i < len; i++)
    {
        hash = hash * 31 + data[i];
        hash ^= hash >> 16;
    }
    return hash;
}

// Generate deterministic 128-bit UUID from device parameters
void generate_device_uuid(uint8_t *uuid_out, const uint8_t *mac, const char *serial_number)
{
    // Create input buffer with device-specific data
    uint8_t input_buffer[64];
    size_t offset = 0;

    // Add MAC address (6 bytes)
    memcpy(&input_buffer[offset], mac, 6);
    offset += 6;

    // Add model string
    const char *model = DEVICE_MODEL;
    size_t model_len = strlen(model);
    if (model_len > 20)
        model_len = 20; // Limit length
    memcpy(&input_buffer[offset], model, model_len);
    offset += model_len;

    // Add version string
    const char *version = DEVICE_VERSION;
    size_t version_len = strlen(version);
    if (version_len > 10)
        version_len = 10; // Limit length
    memcpy(&input_buffer[offset], version, version_len);
    offset += version_len;

    // Add ESP32 chip info for additional uniqueness
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t chip_data[2];
    chip_data[0] = chip_info.features;
    chip_data[1] = (chip_info.cores << 16) | chip_info.revision;
    memcpy(&input_buffer[offset], chip_data, 8);
    offset += 8;

    // Generate 128-bit UUID using 4 different hash seeds
    uint32_t *uuid_words = (uint32_t *)uuid_out;
    uuid_words[0] = htonl(simple_hash(input_buffer, offset, 0x12345678));
    uuid_words[1] = htonl(simple_hash(input_buffer, offset, 0x9ABCDEF0));
    uuid_words[2] = htonl(simple_hash(input_buffer, offset, 0xFEDCBA98));
    uuid_words[3] = htonl(simple_hash(input_buffer, offset, 0x76543210));

    PROTOCOL_LOG_I(TAG, "Generated device UUID from MAC + model + version + chip features");
    PROTOCOL_LOG_BUFFER_HEX(TAG, uuid_out, 16, ESP_LOG_INFO);
}

// Internal function to handle discovery response with flexible header format
static esp_err_t send_discovery_internal(uint16_t packet_id, struct sockaddr_in *dest_addr, bool use_structured_header)
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dest_addr->sin_addr, ip_str, sizeof(ip_str));

    if (use_structured_header)
    {
        // GigE Vision specification compliant structured header format
        uint8_t response[sizeof(gvcp_header_t) + GVBS_DISCOVERY_DATA_SIZE];
        gvcp_header_t *ack_header = (gvcp_header_t *)response;

        uint16_t discovery_data_size_in_words = GVBS_DISCOVERY_DATA_SIZE / 4;
        gvcp_create_ack_header(ack_header, NULL, GVCP_ACK_DISCOVERY, discovery_data_size_in_words);
        ack_header->id = packet_id;

        // Copy bootstrap data
        uint8_t *bootstrap_memory = get_bootstrap_memory();
        memcpy(&response[sizeof(gvcp_header_t)], bootstrap_memory, GVBS_DISCOVERY_DATA_SIZE);

        PROTOCOL_LOG_I(TAG, "GigE Vision SPEC: Sending discovery response to %s:%d with packet ID=0x%04x",
                 ip_str, ntohs(dest_addr->sin_port), ntohs(packet_id));

        // Use gvcp_sendto for consistent error handling and socket management
        esp_err_t result = gvcp_sendto(response, sizeof(response), dest_addr);

        if (result == ESP_OK)
        {
            PROTOCOL_LOG_I(TAG, "GigE Vision SPEC: Discovery response sent (%d bytes) from port 3956", sizeof(response));
            PROTOCOL_LOG_I(TAG, "GigE Vision SPEC: Compliant response: device:3956 -> client:%d", ntohs(dest_addr->sin_port));

            // Set GVSP client address for streaming
            gvsp_set_client_address(dest_addr);
            // Set client connected bit
            gvcp_set_connection_status_bit(GVCP_CONNECTION_STATUS_CLIENT_CONN, true);
        }
        else
        {
            PROTOCOL_LOG_W(TAG, "Discovery response send failed");
        }

        return result;
    }
    else
    {
        // Raw header format (8-byte GigE Vision header + bootstrap data)
        // Used for broadcast compatibility with legacy clients
        uint8_t response[8 + GVBS_DISCOVERY_DATA_SIZE];

        // Construct GigE Vision GVCP header with magic bytes 0x42 0x45
        // These magic bytes identify the packet as GigE Vision format for client compatibility
        response[0] = 0x42;                             // 'B' - GigE Vision magic byte 1
        response[1] = 0x45;                             // 'E' - GigE Vision magic byte 2
        response[2] = GVCP_PACKET_TYPE_ACK;             // 0x00
        response[3] = GVCP_PACKET_FLAG_ACK;             // 0x01
        response[4] = (GVCP_ACK_DISCOVERY >> 8) & 0xFF; // Command high byte
        response[5] = GVCP_ACK_DISCOVERY & 0xFF;        // Command low byte
        response[6] = (packet_id >> 8) & 0xFF;          // Packet ID high byte
        response[7] = packet_id & 0xFF;                 // Packet ID low byte

        // Copy bootstrap memory as discovery payload
        uint8_t *bootstrap_memory = get_bootstrap_memory();
        memcpy(&response[8], bootstrap_memory, GVBS_DISCOVERY_DATA_SIZE);

        PROTOCOL_LOG_I(TAG, "Sending discovery response to %s:%d (ID: 0x%04x, raw format)",
                 ip_str, ntohs(dest_addr->sin_port), ntohs(packet_id));

        // Use gvcp_sendto for consistent error handling
        return gvcp_sendto(response, sizeof(response), dest_addr);
    }
}

// Function to send discovery response with GigE Vision specification compliance
esp_err_t send_gige_spec_discovery_response(uint16_t exact_packet_id, struct sockaddr_in *dest_addr)
{
    return send_discovery_internal(exact_packet_id, dest_addr, true);
}

esp_err_t send_discovery_response(const gvcp_header_t *request_header, struct sockaddr_in *dest_addr, bool is_broadcast)
{
    uint16_t packet_id;
    if (request_header != NULL)
    {
        // Solicited response - echo back the exact packet ID
        packet_id = request_header->id;
        PROTOCOL_LOG_I(TAG, "SOLICITED Response: echoing back packet ID=0x%04x", ntohs(packet_id));
    }
    else
    {
        // Unsolicited broadcast - use current sequence number (incremented per packet)
        packet_id = htons(discovery_broadcast_sequence & 0xFFFF);
        PROTOCOL_LOG_I(TAG, "UNSOLICITED Broadcast: using sequence=%d as unique packet ID=0x%04x",
                 discovery_broadcast_sequence, ntohs(packet_id));
    }

    // Use raw header format for broadcasts (legacy client compatibility)
    return send_discovery_internal(packet_id, dest_addr, false);
}

void handle_discovery_cmd(const gvcp_header_t *header, struct sockaddr_in *client_addr)
{
    uint16_t request_id = ntohs(header->id);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, ip_str, sizeof(ip_str));
    PROTOCOL_LOG_I(TAG, "Discovery SOLICITED from %s:%d, request ID:0x%04x (raw:0x%04x) - MUST echo back exactly",
             ip_str, ntohs(client_addr->sin_port), request_id, header->id);

    // PORT TRACKING: Log the client address before calling response function
    PROTOCOL_LOG_I(TAG, "PORT TRACK 3: client_addr->sin_port = 0x%04x (%d) in handle_discovery_cmd",
             client_addr->sin_port, ntohs(client_addr->sin_port));

    // EXPLICIT: Create and send discovery response with exact packet ID echo
    send_gige_spec_discovery_response(header->id, client_addr);
}

esp_err_t send_discovery_broadcast(void)
{
    if (!discovery_broadcast_enabled)
    {
        return ESP_OK;
    }

    // Send to multiple target addresses to ensure Aravis receives announcements
    const char *target_ips[] = {
        "224.0.0.1",       // All systems multicast
        "255.255.255.255", // Broadcast
        "192.168.1.255",   // Common subnet broadcast
        "192.168.0.255"    // Alternative subnet broadcast
    };

    esp_err_t any_success = ESP_FAIL;
    uint32_t broadcast_cycle = discovery_broadcast_sequence + 1;

    for (int i = 0; i < 4; i++)
    {
        // Increment sequence for each individual broadcast packet to ensure unique packet IDs
        discovery_broadcast_sequence++;

        struct sockaddr_in target_addr;
        memset(&target_addr, 0, sizeof(target_addr));
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(GVCP_PORT);
        inet_pton(AF_INET, target_ips[i], &target_addr.sin_addr);

        PROTOCOL_LOG_D(TAG, "Sending discovery announcement to %s (packet ID: %d)", target_ips[i], discovery_broadcast_sequence);

        // Try broadcast with retries for reliability
        esp_err_t result = ESP_FAIL;
        for (uint32_t retry = 0; retry < discovery_broadcast_retries; retry++)
        {
            result = send_discovery_response(NULL, &target_addr, true);
            if (result == ESP_OK)
            {
                any_success = ESP_OK;
                discovery_broadcasts_sent++;
                break;
            }
            else
            {
                if (retry < discovery_broadcast_retries - 1)
                {
                    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms delay between retries
                }
            }
        }

        if (result != ESP_OK)
        {
            PROTOCOL_LOG_W(TAG, "Failed to send discovery announcement to %s after %d retries", target_ips[i], discovery_broadcast_retries);
        }
    }

    if (any_success != ESP_OK)
    {
        discovery_broadcast_failures++;
        ESP_LOGE(TAG, "All discovery announcements failed for broadcast cycle #%d", broadcast_cycle);
    }
    else
    {
        PROTOCOL_LOG_I(TAG, "Discovery announcements sent successfully (broadcast cycle #%d, packets %d-%d)",
                 broadcast_cycle, broadcast_cycle, discovery_broadcast_sequence);
    }

    return any_success;
}

// Discovery broadcast management functions
void gvcp_enable_discovery_broadcast(bool enable)
{
    discovery_broadcast_enabled = enable;
    PROTOCOL_LOG_I(TAG, "Discovery broadcast %s", enable ? "enabled" : "disabled");
}

void gvcp_set_discovery_broadcast_interval(uint32_t interval_ms)
{
    if (interval_ms >= 1000 && interval_ms <= 30000)
    {
        discovery_broadcast_interval_ms = interval_ms;
        PROTOCOL_LOG_I(TAG, "Discovery broadcast interval set to %d ms", interval_ms);
    }
    else
    {
        ESP_LOGW(TAG, "Invalid broadcast interval %d ms, keeping current %d ms",
                 interval_ms, discovery_broadcast_interval_ms);
    }
}

esp_err_t gvcp_trigger_discovery_broadcast(void)
{
    if (!discovery_broadcast_enabled)
    {
        ESP_LOGW(TAG, "Discovery broadcast is disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (sock < 0)
    {
        ESP_LOGE(TAG, "GVCP socket not available for broadcast");
        return ESP_ERR_INVALID_STATE;
    }

    PROTOCOL_LOG_I(TAG, "Triggering immediate discovery broadcast");
    esp_err_t result = send_discovery_broadcast();
    if (result == ESP_OK)
    {
        last_discovery_broadcast_time = esp_log_timestamp();
    }
    return result;
}

uint32_t gvcp_get_discovery_broadcasts_sent(void)
{
    return discovery_broadcasts_sent;
}

uint32_t gvcp_get_discovery_broadcast_failures(void)
{
    return discovery_broadcast_failures;
}

uint32_t gvcp_get_discovery_broadcast_sequence(void)
{
    return discovery_broadcast_sequence;
}

esp_err_t gvcp_discovery_init(void)
{
    // Initialize discovery state
    discovery_broadcast_enabled = false; // Off by default
    discovery_broadcast_interval_ms = 5000;
    last_discovery_broadcast_time = 0;
    discovery_broadcast_sequence = 0;
    discovery_broadcasts_sent = 0;
    discovery_broadcast_failures = 0;

    PROTOCOL_LOG_I(TAG, "Discovery service initialized");
    return ESP_OK;
}

void gvcp_discovery_process_periodic(void)
{
    if (!discovery_broadcast_enabled || sock < 0)
    {
        return;
    }

    uint32_t current_time = esp_log_timestamp();
    if (current_time - last_discovery_broadcast_time >= discovery_broadcast_interval_ms)
    {
        ESP_LOGD(TAG, "Sending periodic discovery broadcast (sequence #%d)", discovery_broadcast_sequence + 1);
        esp_err_t broadcast_result = send_discovery_broadcast();
        if (broadcast_result == ESP_OK)
        {
            last_discovery_broadcast_time = current_time;
            ESP_LOGD(TAG, "Discovery broadcast sent successfully");
        }
        else
        {
            ESP_LOGW(TAG, "Discovery broadcast failed, will retry next interval");
        }
    }
}