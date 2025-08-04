#include "gvcp_handler.h"
#include "../src/genicam/xml.h"
#include "../src/gvcp/bootstrap.h"
#include "../src/gvcp/discovery.h"
#include "../src/genicam/registers.h"
#include "../src/gvcp/protocol.h"
#include "gvsp_handler.h"
#include "gvcp_statistics.h"
#include "camera_handler.h"
#include "status_led.h"

// Default GVSP packet size if not defined
#ifndef GVSP_DATA_PACKET_SIZE
#define GVSP_DATA_PACKET_SIZE 1400
#endif
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "esp_chip_info.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/param.h>  // For MIN macro

static const char *TAG = "gvcp_handler";

// Forward declarations for internal handler functions
static void handle_read_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr);
static void handle_write_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr);
static void handle_readreg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr);
static void handle_writereg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr);
static void handle_packetresend_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr);
static esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);
static gvcp_result_t gvcp_send_callback_wrapper(const void *data, size_t len, void *addr);
static void gvsp_client_callback_wrapper(void *addr);

int sock = -1; // Made non-static for module access

// Device information constants
#define DEVICE_MANUFACTURER "ESP32GenICam"
#define DEVICE_MODEL "ESP32-CAM-GigE"
#define DEVICE_VERSION "1.0.0"
#define DEVICE_SERIAL "ESP32CAM001"
#define DEVICE_USER_NAME "ESP32Camera"

// Connection status (bit field) - minimal for socket management
// Bit 0: GVCP socket active
static uint32_t connection_status = 0;

esp_err_t gvcp_init(void)
{
    struct sockaddr_in dest_addr;

    // Initialize bootstrap memory
    if (gvcp_bootstrap_init() != GVCP_BOOTSTRAP_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to initialize bootstrap memory");
        return ESP_FAIL;
    }

    // Set up callback functions for the abstracted modules
    genicam_registers_set_bootstrap_callback(gvcp_bootstrap_get_memory);
    gvcp_discovery_set_bootstrap_callback(gvcp_bootstrap_get_memory);

    // Initialize register system
    if (genicam_registers_init() != GENICAM_REGISTERS_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to initialize GenICam registers");
        return ESP_FAIL;
    }

    // Initialize discovery system
    if (gvcp_discovery_init() != GVCP_DISCOVERY_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to initialize GVCP discovery");
        return ESP_FAIL;
    }

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(GVCP_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Socket created");

    // Enable broadcast for both sending and receiving
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0)
    {
        ESP_LOGE(TAG, "Failed to set socket broadcast option");
        close(sock);
        return ESP_FAIL;
    }

    // Enable address reuse for better broadcast handling
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set socket reuse option (non-critical)");
    }

    // Set socket receive timeout to allow responsive broadcasts
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000; // 500ms for responsive broadcast timing
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
    {
        ESP_LOGE(TAG, "Failed to set socket receive timeout");
        close(sock);
        return ESP_FAIL;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Socket bound to port %d", GVCP_PORT);
    ESP_LOGI(TAG, "GVCP socket listening on 0.0.0.0:%d for broadcast and unicast packets", GVCP_PORT);

    // Set GVCP socket active bit
    connection_status |= 0x01;

    // Set the send callback for the protocol layer
    gvcp_set_send_callback(gvcp_send_callback_wrapper);
    
    // Set callbacks for discovery module
    gvcp_discovery_set_gvsp_client_callback(gvsp_client_callback_wrapper);
    gvcp_discovery_set_connection_status_callback(gvcp_set_connection_status_bit);

    return ESP_OK;
}

void handle_gvcp_packet(const uint8_t *packet, int len, struct sockaddr_in *client_addr)
{
    genicam_registers_increment_total_commands();

    // Enhanced packet validation
    if (len < sizeof(gvcp_header_t))
    {
        ESP_LOGE(TAG, "Packet too small for GVCP header: %d bytes", len);
        return;
    }

    // Log raw packet bytes for debugging command field corruption
    ESP_LOGI(TAG, "Raw packet (%d bytes): %02x %02x %02x %02x %02x %02x %02x %02x...", 
             len, packet[0], packet[1], packet[2], packet[3], 
             packet[4], packet[5], packet[6], packet[7]);

    // Validate packet buffer alignment before casting to header
    if ((uintptr_t)packet % sizeof(uint16_t) != 0)
    {
        ESP_LOGW(TAG, "GVCP packet buffer not aligned for header access");
    }

    gvcp_header_t *header = (gvcp_header_t *)packet;

    // Log raw header fields before byte order conversion for debugging
    ESP_LOGI(TAG, "Raw header fields - packet_type:0x%02x flags:0x%02x command:0x%04x(raw) size:0x%04x(raw) id:0x%04x(raw)",
             header->packet_type, header->packet_flags, header->command, header->size, header->id);

    // Extract and validate command field with detailed logging
    uint16_t command_raw = header->command;
    uint16_t command = ntohs(header->command);
    
    ESP_LOGI(TAG, "Command field: raw=0x%04x -> converted=0x%04x", command_raw, command);

    // Validate command is in expected GVCP range
    bool valid_command = false;
    switch (command)
    {
        case GVCP_CMD_DISCOVERY:
        case GVCP_CMD_READ_MEMORY:
        case GVCP_CMD_WRITE_MEMORY:
        case GVCP_CMD_READREG:
        case GVCP_CMD_WRITEREG:
        case GVCP_CMD_PACKETRESEND:
            valid_command = true;
            break;
        default:
            ESP_LOGW(TAG, "Unexpected command value 0x%04x - potential corruption or unknown command", command);
            break;
    }

    if (!valid_command)
    {
        ESP_LOGE(TAG, "COMMAND CORRUPTION DETECTED: 0x%04x is not a valid GVCP command", command);
        ESP_LOGE(TAG, "Raw bytes at command field offset: packet[2]=0x%02x packet[3]=0x%02x", packet[2], packet[3]);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, packet, MIN(len, 16), ESP_LOG_ERROR);
    }

    // Add GVCP protocol validation to check size field interpretation
    if (!gvcp_validate_packet_header(header, len))
    {
        uint16_t size_words = ntohs(header->size);
        uint16_t size_bytes = size_words * 4;
        size_t expected_len = sizeof(gvcp_header_t) + size_bytes;

        ESP_LOGW(TAG, "GVCP packet failed protocol validation - likely size field mismatch");
        ESP_LOGI(TAG, "Packet validation: header.size=%u words (%u bytes), total_len=%d, expected_len=%zu",
                 size_words, size_bytes, len, expected_len);
        (void)expected_len; // Suppress unused variable warning when protocol logging disabled
    }

    const uint8_t *data = packet + sizeof(gvcp_header_t);
    int data_len = len - sizeof(gvcp_header_t);

    ESP_LOGI(TAG, "Processing GVCP command: 0x%04x", command);

    // Only process valid commands to prevent corruption-induced misbehavior
    if (!valid_command)
    {
        ESP_LOGE(TAG, "Dropping packet with invalid/corrupted command 0x%04x", command);
        gvcp_increment_unknown_commands();
        gvcp_send_nack(header, GVCP_ERROR_INVALID_HEADER, client_addr);
        return;
    }

    switch (command)
    {
    case GVCP_CMD_DISCOVERY:
        ESP_LOGI(TAG, "Handling DISCOVERY command (0x%04x)", command);
        gvcp_discovery_handle_command(header, client_addr);
        break;

    case GVCP_CMD_READ_MEMORY:
        ESP_LOGI(TAG, "Handling READ_MEMORY command (0x%04x)", command);
        if (data_len >= 8)
        {
            handle_read_memory_cmd(header, data, client_addr);
        }
        else
        {
            ESP_LOGE(TAG, "READ_MEMORY command too short: %d bytes", data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        }
        break;

    case GVCP_CMD_WRITE_MEMORY:
        ESP_LOGI(TAG, "Handling WRITE_MEMORY command (0x%04x)", command);
        if (data_len >= 4)
        {
            handle_write_memory_cmd(header, data, client_addr);
        }
        else
        {
            ESP_LOGE(TAG, "WRITE_MEMORY command too short: %d bytes", data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        }
        break;

    case GVCP_CMD_READREG:
        ESP_LOGI(TAG, "Handling READREG command (0x%04x)", command);
        if (data_len >= 4)
        {
            handle_readreg_cmd(header, data, data_len, client_addr);
        }
        else
        {
            ESP_LOGE(TAG, "READREG command too short: %d bytes", data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        }
        break;

    case GVCP_CMD_WRITEREG:
        ESP_LOGI(TAG, "Handling WRITEREG command (0x%04x)", command);
        if (data_len >= 8)
        {
            handle_writereg_cmd(header, data, data_len, client_addr);
        }
        else
        {
            ESP_LOGE(TAG, "WRITEREG command too short: %d bytes", data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        }
        break;

    case GVCP_CMD_PACKETRESEND:
        ESP_LOGI(TAG, "Handling PACKETRESEND command (0x%04x)", command);
        handle_packetresend_cmd(header, data, client_addr);
        break;

    default:
        // This should never happen due to validation above, but keeping as safety net
        gvcp_increment_unknown_commands();
        ESP_LOGE(TAG, "BUG: Unknown GVCP command reached default case: 0x%04x (validation failed)", command);
        gvcp_send_nack(header, GVCP_ERROR_NOT_IMPLEMENTED, client_addr);
        break;
    }
}

void gvcp_task(void *pvParameters)
{
    uint8_t rx_buffer[2048];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);

    ESP_LOGI(TAG, "GVCP task started");

    // Add this task to watchdog monitoring
    esp_task_wdt_add(NULL);

    while (1)
    {
        // Feed the watchdog to prevent timeout
        esp_task_wdt_reset();
        // Socket error handling moved to protocol module

        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Timeout occurred - this is normal, continue loop
                gvcp_discovery_process_periodic();
                continue;
            }
            else
            {
                ESP_LOGE(TAG, "GVCP recvfrom failed: errno %d (%s)", errno, strerror(errno));
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }
        else if (len == 0)
        {
            ESP_LOGW(TAG, "GVCP received empty packet");
            continue;
        }
        else
        {
            // Process the packet
            handle_gvcp_packet((uint8_t *)rx_buffer, len, &source_addr);
        }

        // Periodic discovery broadcast processing
        gvcp_discovery_process_periodic();

        // Small delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Internal ESP32 network send function
static esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr) {
    if (sock < 0) {
        ESP_LOGE(TAG, "GVCP socket not initialized");
        return ESP_FAIL;
    }
    
    int bytes_sent = sendto(sock, data, data_len, 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
    if (bytes_sent < 0) {
        ESP_LOGE(TAG, "Failed to send GVCP response: errno %d", errno);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// Callback wrapper to bridge gvcp_protocol.c with ESP32 socket implementation
static gvcp_result_t gvcp_send_callback_wrapper(const void *data, size_t len, void *addr) {
    struct sockaddr_in *client_addr = (struct sockaddr_in *)addr;
    esp_err_t result = gvcp_sendto(data, len, client_addr);
    return (result == ESP_OK) ? GVCP_RESULT_SUCCESS : GVCP_RESULT_SEND_FAILED;
}

// Wrapper for GVSP client address callback
static void gvsp_client_callback_wrapper(void *addr) {
    struct sockaddr_in *client_addr = (struct sockaddr_in *)addr;
    gvsp_set_client_address(client_addr);
}

// Internal handler for READ_MEMORY commands
static void handle_read_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr) {
    uint32_t address = ntohl(*(uint32_t *)data);
    uint16_t length = ntohs(*(uint16_t *)(data + 4));
    
    ESP_LOGI(TAG, "READ_MEMORY: address=0x%08x, length=%d", address, length);
    
    uint8_t response[sizeof(gvcp_header_t) + length];
    gvcp_header_t *ack_header = (gvcp_header_t *)response;
    uint8_t *response_data = response + sizeof(gvcp_header_t);
    
    // Use the abstracted register interface
    genicam_registers_result_t result = genicam_registers_read_memory(address, response_data, length);
    
    if (result == GENICAM_REGISTERS_SUCCESS) {
        gvcp_create_ack_header(ack_header, header, GVCP_ACK_READ_MEMORY, GVCP_BYTES_TO_WORDS(length));
        gvcp_sendto(response, sizeof(response), client_addr);
    } else {
        uint16_t error_code = (result == GENICAM_REGISTERS_INVALID_ADDRESS) ? 
            GVCP_ERROR_INVALID_ADDRESS : GVCP_ERROR_ACCESS_DENIED;
        gvcp_send_nack(header, error_code, client_addr);
    }
}

// Internal handler for WRITE_MEMORY commands
static void handle_write_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr) {
    uint32_t address = ntohl(*(uint32_t *)data);
    uint16_t data_len = ntohs(header->size) * 4 - 4; // Total data minus address field
    
    ESP_LOGI(TAG, "WRITE_MEMORY: address=0x%08x, length=%d", address, data_len);
    
    // Use the abstracted register interface
    genicam_registers_result_t result = genicam_registers_write_memory(address, data + 4, data_len);
    
    if (result == GENICAM_REGISTERS_SUCCESS) {
        gvcp_header_t ack_header;
        gvcp_create_ack_header(&ack_header, header, GVCP_ACK_WRITE_MEMORY, 0);
        gvcp_sendto(&ack_header, sizeof(ack_header), client_addr);
    } else {
        uint16_t error_code = (result == GENICAM_REGISTERS_INVALID_ADDRESS) ? 
            GVCP_ERROR_INVALID_ADDRESS : GVCP_ERROR_ACCESS_DENIED;
        gvcp_send_nack(header, error_code, client_addr);
    }
}

// Internal handler for READREG commands
static void handle_readreg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr) {
    uint32_t address = ntohl(*(uint32_t *)data);
    uint32_t value = 0;
    
    ESP_LOGI(TAG, "READREG: address=0x%08x", address);
    
    genicam_registers_result_t result = genicam_registers_read(address, &value);
    
    if (result == GENICAM_REGISTERS_SUCCESS) {
        uint8_t response[sizeof(gvcp_header_t) + 4];
        gvcp_header_t *ack_header = (gvcp_header_t *)response;
        uint32_t *response_data = (uint32_t *)(response + sizeof(gvcp_header_t));
        
        gvcp_create_ack_header(ack_header, header, GVCP_ACK_READREG, 1);
        *response_data = htonl(value);
        gvcp_sendto(response, sizeof(response), client_addr);
    } else {
        uint16_t error_code = (result == GENICAM_REGISTERS_INVALID_ADDRESS) ? 
            GVCP_ERROR_INVALID_ADDRESS : GVCP_ERROR_ACCESS_DENIED;
        gvcp_send_nack(header, error_code, client_addr);
    }
}

// Internal handler for WRITEREG commands
static void handle_writereg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr) {
    uint32_t address = ntohl(*(uint32_t *)data);
    uint32_t value = ntohl(*(uint32_t *)(data + 4));
    
    ESP_LOGI(TAG, "WRITEREG: address=0x%08x, value=0x%08x", address, value);
    
    genicam_registers_result_t result = genicam_registers_write(address, value);
    
    if (result == GENICAM_REGISTERS_SUCCESS) {
        gvcp_header_t ack_header;
        gvcp_create_ack_header(&ack_header, header, GVCP_ACK_WRITEREG, 0);
        gvcp_sendto(&ack_header, sizeof(ack_header), client_addr);
    } else {
        uint16_t error_code;
        if (result == GENICAM_REGISTERS_INVALID_ADDRESS) {
            error_code = GVCP_ERROR_INVALID_ADDRESS;
        } else if (result == GENICAM_REGISTERS_WRITE_PROTECTED) {
            error_code = GVCP_ERROR_WRITE_PROTECT;
        } else {
            error_code = GVCP_ERROR_ACCESS_DENIED;
        }
        gvcp_send_nack(header, error_code, client_addr);
    }
}

// Internal handler for PACKETRESEND commands (basic implementation)
static void handle_packetresend_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr) {
    ESP_LOGI(TAG, "PACKETRESEND: command received (basic implementation)");
    
    // For now, send a simple ACK - packet resend functionality can be enhanced later
    gvcp_header_t ack_header;
    gvcp_create_ack_header(&ack_header, header, GVCP_ACK_PACKETRESEND, 0);
    gvcp_sendto(&ack_header, sizeof(ack_header), client_addr);
}
