#include "gvcp_handler.h"
#include "genicam_xml.h"
#include "gvsp_handler.h"
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

static const char *TAG = "gvcp_handler";

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

    // Validate GenICam XML data before initialization
    ESP_LOGI(TAG, "Validating GenICam XML data...");
    if (genicam_xml_size == 0)
    {
        ESP_LOGE(TAG, "CRITICAL: genicam_xml_size is 0!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GenICam XML validation: size=%d bytes, first chars: %.32s",
             genicam_xml_size, (const char *)genicam_xml_data);

    // Initialize bootstrap memory
    init_bootstrap_memory();

    // Initialize all modules
    gvcp_discovery_init();
    gvcp_registers_init();

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

    return ESP_OK;
}

void handle_gvcp_packet(const uint8_t *packet, int len, struct sockaddr_in *client_addr)
{
    gvcp_increment_total_commands();

    // Enhanced packet validation
    if (len < sizeof(gvcp_header_t))
    {
        ESP_LOGE(TAG, "Packet too small for GVCP header: %d bytes", len);
        return;
    }

    gvcp_header_t *header = (gvcp_header_t *)packet;
    const uint8_t *data = packet + sizeof(gvcp_header_t);
    int data_len = len - sizeof(gvcp_header_t);

    uint16_t command = ntohs(header->command);
    ESP_LOGI(TAG, "Received GVCP command: 0x%04x", command);

    switch (command)
    {
    case GVCP_CMD_DISCOVERY:
        handle_discovery_cmd(header, client_addr);
        break;

    case GVCP_CMD_READ_MEMORY:
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
        if (data_len >= 4)
        {
            handle_readreg_cmd(header, data, client_addr);
        }
        else
        {
            ESP_LOGE(TAG, "READREG command too short: %d bytes", data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        }
        break;

    case GVCP_CMD_WRITEREG:
        if (data_len >= 8)
        {
            handle_writereg_cmd(header, data, client_addr);
        }
        else
        {
            ESP_LOGE(TAG, "WRITEREG command too short: %d bytes", data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        }
        break;

    case GVCP_CMD_PACKETRESEND:
        handle_packetresend_cmd(header, data, client_addr);
        break;

    default:
        gvcp_increment_unknown_commands();
        ESP_LOGW(TAG, "Unknown GVCP command: 0x%04x", command);
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
