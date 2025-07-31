#include "gvcp_handler.h"
#include "genicam_xml.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "gvcp_handler";

static int sock = -1;
static uint8_t bootstrap_memory[GVBS_DISCOVERY_DATA_SIZE];

// Device information constants
#define DEVICE_MANUFACTURER "ESP32GenICam"
#define DEVICE_MODEL "ESP32-CAM-GigE"
#define DEVICE_VERSION "1.0.0"
#define DEVICE_SERIAL "ESP32CAM001"
#define DEVICE_USER_NAME "ESP32Camera"
#define XML_URL "Local:0x10000;0x2000;0x10000"

// XML memory mapping
#define XML_BASE_ADDRESS 0x10000

static void init_bootstrap_memory(void)
{
    memset(bootstrap_memory, 0, sizeof(bootstrap_memory));
    
    // Version register (Major=1, Minor=0)
    uint32_t version = htonl(0x00010000);
    memcpy(&bootstrap_memory[GVBS_VERSION_OFFSET], &version, 4);
    
    // Device mode (big endian, UTF8)
    uint32_t device_mode = htonl(0x80000000);
    memcpy(&bootstrap_memory[GVBS_DEVICE_MODE_OFFSET], &device_mode, 4);
    
    // Get MAC address
    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    if (ret == ESP_OK) {
        uint32_t mac_high = htonl((mac[0] << 8) | mac[1]);
        uint32_t mac_low = htonl((mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5]);
        memcpy(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_HIGH_OFFSET], &mac_high, 4);
        memcpy(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_LOW_OFFSET], &mac_low, 4);
    }
    
    // Get current IP address
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            uint32_t ip = ip_info.ip.addr;
            memcpy(&bootstrap_memory[GVBS_CURRENT_IP_ADDRESS_OFFSET], &ip, 4);
        }
    }
    
    // Device strings
    strncpy((char*)&bootstrap_memory[GVBS_MANUFACTURER_NAME_OFFSET], DEVICE_MANUFACTURER, 32);
    strncpy((char*)&bootstrap_memory[GVBS_MODEL_NAME_OFFSET], DEVICE_MODEL, 32);
    strncpy((char*)&bootstrap_memory[GVBS_DEVICE_VERSION_OFFSET], DEVICE_VERSION, 32);
    strncpy((char*)&bootstrap_memory[GVBS_SERIAL_NUMBER_OFFSET], DEVICE_SERIAL, 16);
    strncpy((char*)&bootstrap_memory[GVBS_USER_DEFINED_NAME_OFFSET], DEVICE_USER_NAME, 16);
    
    // XML URL
    strncpy((char*)&bootstrap_memory[GVBS_XML_URL_0_OFFSET], XML_URL, 512);
}

esp_err_t gvcp_init(void)
{
    struct sockaddr_in dest_addr;
    
    // Initialize bootstrap memory
    init_bootstrap_memory();
    
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(GVCP_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Socket created");

    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket broadcast option");
        close(sock);
        return ESP_FAIL;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Socket bound to port %d", GVCP_PORT);

    return ESP_OK;
}

static void handle_discovery_cmd(const gvcp_header_t *header, struct sockaddr_in *client_addr)
{
    ESP_LOGI(TAG, "Handling GVCP Discovery Command");
    
    // Create discovery ACK response
    uint8_t response[sizeof(gvcp_header_t) + GVBS_DISCOVERY_DATA_SIZE];
    gvcp_header_t *ack_header = (gvcp_header_t*)response;
    
    ack_header->packet_type = GVCP_PACKET_TYPE_ACK;
    ack_header->packet_flags = 0;
    ack_header->command = htons(GVCP_ACK_DISCOVERY);
    ack_header->size = htons(GVBS_DISCOVERY_DATA_SIZE);
    ack_header->id = header->id; // Echo back the packet ID
    
    // Copy bootstrap data
    memcpy(&response[sizeof(gvcp_header_t)], bootstrap_memory, GVBS_DISCOVERY_DATA_SIZE);
    
    // Send response
    int err = sendto(sock, response, sizeof(response), 0,
                    (struct sockaddr *)client_addr, sizeof(*client_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Error sending discovery ACK: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "Sent discovery ACK (%d bytes)", sizeof(response));
    }
}

static void handle_read_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    if (ntohs(header->size) < 8) {
        ESP_LOGE(TAG, "Invalid read memory command size");
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    uint32_t size = ntohl(*(uint32_t*)(data + 4));
    
    ESP_LOGI(TAG, "Read memory: addr=0x%08x, size=%d", address, size);
    
    // Limit read size for safety
    if (size > 512) {
        size = 512;
    }
    
    // Create read memory ACK response
    uint8_t response[sizeof(gvcp_header_t) + 4 + size];
    gvcp_header_t *ack_header = (gvcp_header_t*)response;
    
    ack_header->packet_type = GVCP_PACKET_TYPE_ACK;
    ack_header->packet_flags = 0;
    ack_header->command = htons(GVCP_ACK_READ_MEMORY);
    ack_header->size = htons(4 + size);
    ack_header->id = header->id;
    
    // Copy address back
    *(uint32_t*)&response[sizeof(gvcp_header_t)] = htonl(address);
    
    // Copy memory data
    uint8_t *data_ptr = &response[sizeof(gvcp_header_t) + 4];
    
    if (address < GVBS_DISCOVERY_DATA_SIZE && address + size <= GVBS_DISCOVERY_DATA_SIZE) {
        // Bootstrap memory region
        memcpy(data_ptr, &bootstrap_memory[address], size);
    } else if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size) {
        // GenICam XML memory region
        uint32_t xml_offset = address - XML_BASE_ADDRESS;
        uint32_t xml_read_size = size;
        
        if (xml_offset + xml_read_size > genicam_xml_size) {
            xml_read_size = genicam_xml_size - xml_offset;
        }
        
        if (xml_read_size > 0) {
            memcpy(data_ptr, &genicam_xml_data[xml_offset], xml_read_size);
        }
        
        // Fill remaining with zeros if needed
        if (xml_read_size < size) {
            memset(data_ptr + xml_read_size, 0, size - xml_read_size);
        }
    } else {
        // Unknown memory region - return zeros
        memset(data_ptr, 0, size);
    }
    
    // Send response
    int err = sendto(sock, response, sizeof(gvcp_header_t) + 4 + size, 0,
                    (struct sockaddr *)client_addr, sizeof(*client_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Error sending read memory ACK: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "Sent read memory ACK (%d bytes)", sizeof(gvcp_header_t) + 4 + size);
    }
}

static void handle_write_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    if (ntohs(header->size) < 4) {
        ESP_LOGE(TAG, "Invalid write memory command size");
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    uint32_t size = ntohs(header->size) - 4;
    const uint8_t *write_data = data + 4;
    
    ESP_LOGI(TAG, "Write memory: addr=0x%08x, size=%d", address, size);
    
    // For now, only allow writes to certain bootstrap registers (user-defined name, etc.)
    bool write_allowed = false;
    if (address >= GVBS_USER_DEFINED_NAME_OFFSET && 
        address + size <= GVBS_USER_DEFINED_NAME_OFFSET + 16) {
        write_allowed = true;
    }
    
    if (write_allowed && address + size <= GVBS_DISCOVERY_DATA_SIZE) {
        memcpy(&bootstrap_memory[address], write_data, size);
        ESP_LOGI(TAG, "Write successful to address 0x%08x", address);
    } else {
        ESP_LOGW(TAG, "Write denied to address 0x%08x (read-only or out of range)", address);
    }
    
    // Create write memory ACK response
    uint8_t response[sizeof(gvcp_header_t) + 4];
    gvcp_header_t *ack_header = (gvcp_header_t*)response;
    
    ack_header->packet_type = GVCP_PACKET_TYPE_ACK;
    ack_header->packet_flags = 0;
    ack_header->command = htons(GVCP_ACK_WRITE_MEMORY);
    ack_header->size = htons(4);
    ack_header->id = header->id;
    
    // Copy address back
    *(uint32_t*)&response[sizeof(gvcp_header_t)] = htonl(address);
    
    // Send response
    int err = sendto(sock, response, sizeof(response), 0,
                    (struct sockaddr *)client_addr, sizeof(*client_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Error sending write memory ACK: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "Sent write memory ACK");
    }
}

static void handle_gvcp_packet(const uint8_t *packet, int len, struct sockaddr_in *client_addr)
{
    if (len < sizeof(gvcp_header_t)) {
        ESP_LOGE(TAG, "Packet too small for GVCP header");
        return;
    }
    
    const gvcp_header_t *header = (const gvcp_header_t*)packet;
    const uint8_t *data = packet + sizeof(gvcp_header_t);
    
    uint16_t command = ntohs(header->command);
    uint16_t packet_id = ntohs(header->id);
    
    ESP_LOGI(TAG, "GVCP packet: type=0x%02x, cmd=0x%04x, id=0x%04x",
             header->packet_type, command, packet_id);
    
    switch (command) {
        case GVCP_CMD_DISCOVERY:
            handle_discovery_cmd(header, client_addr);
            break;
            
        case GVCP_CMD_READ_MEMORY:
            handle_read_memory_cmd(header, data, client_addr);
            break;
            
        case GVCP_CMD_WRITE_MEMORY:
            handle_write_memory_cmd(header, data, client_addr);
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled GVCP command: 0x%04x", command);
            break;
    }
}

void gvcp_task(void *pvParameters)
{
    char rx_buffer[1024];
    char addr_str[128];

    ESP_LOGI(TAG, "GVCP task started");

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        } else if (len > 0) {
            inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
            
            ESP_LOGI(TAG, "Received %d bytes from %s:%d", len, addr_str, ntohs(source_addr.sin_port));
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, len, ESP_LOG_DEBUG);

            handle_gvcp_packet((uint8_t*)rx_buffer, len, &source_addr);
        }
    }

    if (sock != -1) {
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(sock, 0);
        close(sock);
    }
    vTaskDelete(NULL);
}