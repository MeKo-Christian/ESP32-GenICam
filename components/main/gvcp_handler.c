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
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

static const char *TAG = "gvcp_handler";

static int sock = -1;
// Bootstrap memory needs to be large enough to hold XML URL at offset 0x200 + URL size
#define BOOTSTRAP_MEMORY_SIZE 0x300
static uint8_t bootstrap_memory[BOOTSTRAP_MEMORY_SIZE];

// Forward declarations
static esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);


// Error handling statistics
static uint32_t total_commands_received = 0;
static uint32_t total_errors_sent = 0;
static uint32_t total_unknown_commands = 0;

// Socket health monitoring
static uint32_t gvcp_socket_error_count = 0;
static uint32_t gvcp_max_socket_errors = 3;
static uint32_t gvcp_last_socket_recreation = 0;
static uint32_t gvcp_socket_recreation_interval_ms = 15000; // Min 15s between recreations

// Discovery broadcast configuration
static bool discovery_broadcast_enabled = false;
static uint32_t discovery_broadcast_interval_ms = 5000;
static uint32_t last_discovery_broadcast_time = 0;
static uint32_t discovery_broadcast_sequence = 0;
static uint32_t discovery_broadcast_retries = 3; // Number of retries for broadcast
static uint32_t discovery_broadcasts_sent = 0;
static uint32_t discovery_broadcast_failures = 0;

// Device information constants
#define DEVICE_MANUFACTURER "ESP32GenICam"
#define DEVICE_MODEL "ESP32-CAM-GigE"
#define DEVICE_VERSION "1.0.0"
#define DEVICE_SERIAL "ESP32CAM001"
#define DEVICE_USER_NAME "ESP32Camera"
#define XML_URL "Local:0x10000;0x2000;"

// XML memory mapping
#define XML_BASE_ADDRESS 0x10000

// Acquisition control state
static uint32_t acquisition_mode = 0; // 0 = Continuous
static uint32_t acquisition_start_reg = 0;
static uint32_t acquisition_stop_reg = 0;

// Stream control parameters
static uint32_t packet_delay_us = 1000; // Inter-packet delay in microseconds (default 1ms)
static uint32_t frame_rate_fps = 1; // Frame rate in FPS (default 1 FPS)
static uint32_t packet_size = GVSP_DATA_PACKET_SIZE; // Data packet size (default 1400)
static uint32_t stream_status = 0; // Stream status register

// Connection status (bit field)
// Bit 0: GVCP socket active
// Bit 1: GVSP socket active  
// Bit 2: Client connected
// Bit 3: Streaming active
static uint32_t connection_status = 0;

static void init_bootstrap_memory(void)
{
    memset(bootstrap_memory, 0, sizeof(bootstrap_memory));
    
    // Version register (Major=1, Minor=0)
    uint32_t version = htonl(0x00010000);
    memcpy(&bootstrap_memory[GVBS_VERSION_OFFSET], &version, 4);
    
    // Device mode (big endian, UTF8)
    uint32_t device_mode = htonl(0x80000000);
    memcpy(&bootstrap_memory[GVBS_DEVICE_MODE_OFFSET], &device_mode, 4);
    
    // Device capabilities register (indicate GigE Vision support)
    uint32_t device_capabilities = htonl(0x00000001); // Bit 0: GigE Vision supported
    memcpy(&bootstrap_memory[GVBS_DEVICE_CAPABILITIES_OFFSET], &device_capabilities, 4);
    
    // Get MAC address - encode according to GigE Vision spec
    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    if (ret == ESP_OK) {
        // GigE Vision MAC address format: high = first 2 bytes, low = last 4 bytes
        // Store in network byte order (big endian)
        uint32_t mac_high = htonl((uint32_t)(mac[0] << 8) | mac[1]);
        uint32_t mac_low = htonl((uint32_t)(mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5]);
        memcpy(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_HIGH_OFFSET], &mac_high, 4);
        memcpy(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_LOW_OFFSET], &mac_low, 4);
    }
    
    // Get current IP address and network configuration
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            uint32_t ip = ip_info.ip.addr;
            uint32_t netmask = ip_info.netmask.addr;
            uint32_t gateway = ip_info.gw.addr;
            
            // Convert to network byte order (big-endian) for GigE Vision compliance
            uint32_t ip_be = htonl(ip);
            uint32_t netmask_be = htonl(netmask);
            uint32_t gateway_be = htonl(gateway);
            
            memcpy(&bootstrap_memory[GVBS_CURRENT_IP_ADDRESS_OFFSET], &ip_be, 4);
            memcpy(&bootstrap_memory[GVBS_CURRENT_SUBNET_MASK_OFFSET], &netmask_be, 4);
            memcpy(&bootstrap_memory[GVBS_CURRENT_DEFAULT_GATEWAY_OFFSET], &gateway_be, 4);
            
            // Supported IP configuration register (static IP, DHCP, etc.)
            // Bit 0: Manual IP, Bit 1: DHCP, Bit 2: AutoIP, Bit 3: Persistent IP
            uint32_t supported_ip_config = htonl(0x00000006); // DHCP + AutoIP supported
            memcpy(&bootstrap_memory[GVBS_SUPPORTED_IP_CONFIG_OFFSET], &supported_ip_config, 4);
            
            // Current IP configuration register (which method is currently active)
            uint32_t current_ip_config = htonl(0x00000002); // DHCP currently active
            memcpy(&bootstrap_memory[GVBS_CURRENT_IP_CONFIG_OFFSET], &current_ip_config, 4);
            
            // Link speed register (WiFi typically 54 Mbps for 802.11g, 150+ for 802.11n)
            uint32_t link_speed = htonl(54000000); // 54 Mbps in bps
            memcpy(&bootstrap_memory[GVBS_LINK_SPEED_OFFSET], &link_speed, 4);
        }
    }
    
    // Device strings (ensure proper null termination)
    memset(&bootstrap_memory[GVBS_MANUFACTURER_NAME_OFFSET], 0, 32);
    memset(&bootstrap_memory[GVBS_MODEL_NAME_OFFSET], 0, 32);
    memset(&bootstrap_memory[GVBS_DEVICE_VERSION_OFFSET], 0, 32);
    memset(&bootstrap_memory[GVBS_SERIAL_NUMBER_OFFSET], 0, 16);
    memset(&bootstrap_memory[GVBS_USER_DEFINED_NAME_OFFSET], 0, 16);
    
    strncpy((char*)&bootstrap_memory[GVBS_MANUFACTURER_NAME_OFFSET], DEVICE_MANUFACTURER, 31);
    strncpy((char*)&bootstrap_memory[GVBS_MODEL_NAME_OFFSET], DEVICE_MODEL, 31);
    strncpy((char*)&bootstrap_memory[GVBS_DEVICE_VERSION_OFFSET], DEVICE_VERSION, 31);
    strncpy((char*)&bootstrap_memory[GVBS_SERIAL_NUMBER_OFFSET], DEVICE_SERIAL, 15);
    strncpy((char*)&bootstrap_memory[GVBS_USER_DEFINED_NAME_OFFSET], DEVICE_USER_NAME, 15);
    
    // XML URL (safe size: BOOTSTRAP_MEMORY_SIZE - GVBS_XML_URL_0_OFFSET)
    size_t xml_url_max_size = BOOTSTRAP_MEMORY_SIZE - GVBS_XML_URL_0_OFFSET;
    strncpy((char*)&bootstrap_memory[GVBS_XML_URL_0_OFFSET], XML_URL, xml_url_max_size - 1);
    bootstrap_memory[GVBS_XML_URL_0_OFFSET + xml_url_max_size - 1] = '\0'; // Ensure null termination
}

esp_err_t gvcp_send_nack(const gvcp_header_t *original_header, uint16_t error_code, struct sockaddr_in *client_addr)
{
    if (original_header == NULL || client_addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t response[sizeof(gvcp_header_t) + 2];
    gvcp_header_t *nack_header = (gvcp_header_t*)response;
    
    // Create NACK response header
    nack_header->packet_type = GVCP_PACKET_TYPE_ERROR;
    nack_header->packet_flags = 0;
    nack_header->command = original_header->command; // Echo back original command
    nack_header->size = htons(2); // Error code size
    nack_header->id = original_header->id; // Echo back packet ID
    
    // Add error code
    *(uint16_t*)&response[sizeof(gvcp_header_t)] = htons(error_code);
    
    // Send NACK response
    esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending NACK response");
        return ESP_FAIL;
    } else {
        total_errors_sent++;
        ESP_LOGW(TAG, "Sent NACK response for command 0x%04x with error code 0x%04x", 
                 ntohs(original_header->command), error_code);
        return ESP_OK;
    }
}

esp_err_t gvcp_init(void)
{
    struct sockaddr_in dest_addr;
    
    // Validate GenICam XML data before initialization
    ESP_LOGI(TAG, "Validating GenICam XML data...");
    if (genicam_xml_size == 0) {
        ESP_LOGE(TAG, "CRITICAL: genicam_xml_size is 0!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GenICam XML validation: size=%d bytes, first chars: %.32s", 
             genicam_xml_size, (const char*)genicam_xml_data);
    
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

    // Enable broadcast for both sending and receiving
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket broadcast option");
        close(sock);
        return ESP_FAIL;
    }
    
    // Enable address reuse for better broadcast handling
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ESP_LOGW(TAG, "Failed to set socket reuse option (non-critical)");
    }

    // Phase 2B: Join multicast group for broadcast address reception
    // This attempts to register the broadcast address as a multicast group
    // to bypass WiFi driver broadcast filtering
    ESP_LOGI(TAG, "Phase 2B: Starting multicast group registration for broadcast reception");
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    ESP_LOGI(TAG, "Phase 2B: WiFi netif handle: %p", netif);
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            // Calculate broadcast address for current subnet
            uint32_t broadcast_addr = (ip_info.ip.addr & ip_info.netmask.addr) | (~ip_info.netmask.addr);
            
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = broadcast_addr;
            mreq.imr_interface.s_addr = ip_info.ip.addr;
            
            ESP_LOGI(TAG, "Phase 2B: Attempting multicast group join for broadcast 0x%08lx", broadcast_addr);
            if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0) {
                ESP_LOGI(TAG, "Phase 2B: Successfully joined multicast group for broadcast reception");
            } else {
                ESP_LOGW(TAG, "Phase 2B: Failed to join multicast group (errno %d) - continuing with standard broadcast", errno);
            }
        } else {
            ESP_LOGW(TAG, "Phase 2B: Could not get IP info for multicast group registration");
        }
    } else {
        ESP_LOGW(TAG, "Phase 2B: Could not get WiFi interface for multicast group registration");
    }

    // Set socket receive timeout to allow responsive broadcasts
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000; // 500ms for responsive broadcast timing
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket receive timeout");
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
    ESP_LOGI(TAG, "GVCP socket listening on 0.0.0.0:%d for broadcast and unicast packets", GVCP_PORT);

    // Set GVCP socket active bit
    connection_status |= 0x01;

    // Send initial discovery broadcast to announce presence (only if enabled)
    if (discovery_broadcast_enabled) {
        ESP_LOGI(TAG, "GVCP initialized, sending initial discovery broadcast");
        esp_err_t broadcast_result = gvcp_trigger_discovery_broadcast();
        if (broadcast_result != ESP_OK) {
            ESP_LOGW(TAG, "Initial discovery broadcast failed, will retry in periodic cycle");
        }
    } else {
        ESP_LOGI(TAG, "GVCP initialized, discovery broadcasts disabled by default");
    }

    return ESP_OK;
}

// GVCP socket recreation for network failure recovery
static esp_err_t gvcp_recreate_socket(void)
{
    uint32_t current_time = esp_log_timestamp();
    
    // Rate limiting: don't recreate socket too frequently
    if (current_time - gvcp_last_socket_recreation < gvcp_socket_recreation_interval_ms) {
        ESP_LOGW(TAG, "GVCP socket recreation rate limited, skipping");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "Recreating GVCP socket due to network errors");
    
    // Close existing socket
    if (sock >= 0) {
        close(sock);
        sock = -1;
        connection_status &= ~0x01; // Clear GVCP socket active bit
    }
    
    // Recreate socket
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(GVCP_PORT);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to recreate GVCP socket: errno %d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GVCP socket recreated");

    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket broadcast option after recreation");
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }

    // Set socket receive timeout to allow responsive broadcasts
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000; // 500ms for responsive broadcast timing
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket receive timeout after recreation");
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "GVCP socket unable to bind after recreation: errno %d", errno);
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }
    
    // Initialize bootstrap memory again
    init_bootstrap_memory();
    
    // Reset socket error count and update status
    gvcp_socket_error_count = 0;
    gvcp_last_socket_recreation = current_time;
    connection_status |= 0x01; // Set GVCP socket active bit
    
    ESP_LOGI(TAG, "GVCP socket successfully recreated and bound to port %d", GVCP_PORT);
    return ESP_OK;
}

// Helper function for GVCP sendto with error handling
static esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr)
{
    if (sock < 0) {
        ESP_LOGE(TAG, "Invalid GVCP socket for transmission");
        gvcp_socket_error_count++;
        return ESP_FAIL;
    }
    
    int err = sendto(sock, data, data_len, 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
    if (err < 0) {
        gvcp_socket_error_count++;
        ESP_LOGW(TAG, "GVCP sendto failed: errno %d (%s)", errno, strerror(errno));
        
        // Check for specific network errors that indicate socket issues
        if (errno == EBADF || errno == ENOTSOCK || errno == ENETDOWN || errno == ENETUNREACH) {
            if (gvcp_socket_error_count >= gvcp_max_socket_errors) {
                ESP_LOGW(TAG, "Max GVCP socket errors reached, attempting socket recreation");
                esp_err_t recreate_result = gvcp_recreate_socket();
                if (recreate_result == ESP_OK) {
                    // Retry once with new socket
                    err = sendto(sock, data, data_len, 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
                    if (err >= 0) {
                        return ESP_OK;
                    }
                }
            }
        }
        return ESP_FAIL;
    } else {
        // Reset socket error count on successful send
        if (gvcp_socket_error_count > 0) {
            gvcp_socket_error_count = 0;
        }
        return ESP_OK;
    }
}

// Simple function to send discovery response with exact packet ID using the same socket
static esp_err_t send_discovery_response_with_exact_id(uint16_t exact_packet_id, struct sockaddr_in *dest_addr)
{
    // Create discovery ACK response
    uint8_t response[sizeof(gvcp_header_t) + GVBS_DISCOVERY_DATA_SIZE];
    gvcp_header_t *ack_header = (gvcp_header_t*)response;
    
    ack_header->packet_type = GVCP_PACKET_TYPE_ACK;
    ack_header->packet_flags = 0;
    ack_header->command = htons(GVCP_ACK_DISCOVERY);
    ack_header->size = htons(GVBS_DISCOVERY_DATA_SIZE);
    
    // CRITICAL FIX: Echo back the EXACT packet ID from the request
    ack_header->id = exact_packet_id;
    
    // Copy bootstrap data
    memcpy(&response[sizeof(gvcp_header_t)], bootstrap_memory, GVBS_DISCOVERY_DATA_SIZE);
    
    ESP_LOGI(TAG, "SIMPLE FIX: Sending discovery response to %s:%d with packet ID=0x%04x", 
             inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), ntohs(ack_header->id));
    
    // PORT TRACKING: Log the destination address just before sendto
    ESP_LOGI(TAG, "PORT TRACK 4: dest_addr->sin_port = 0x%04x (%d) before sendto", 
             dest_addr->sin_port, ntohs(dest_addr->sin_port));
    ESP_LOGI(TAG, "PORT TRACK 4: Destination IP = %s", inet_ntoa(dest_addr->sin_addr));
    
    // SIMPLE ARAVIS FIX: Use the same socket (sock) for both recvfrom() and sendto()
    // This automatically handles port matching without binding conflicts
    if (sock < 0) {
        ESP_LOGE(TAG, "Invalid GVCP socket for discovery response");
        return ESP_FAIL;
    }
    
    // Send response using the same socket - dest_addr contains the exact address from recvfrom()
    int err = sendto(sock, response, sizeof(response), 0, 
                     (struct sockaddr *)dest_addr, sizeof(*dest_addr));
    
    if (err < 0) {
        ESP_LOGW(TAG, "Discovery response send failed, errno: %d (%s)", errno, strerror(errno));
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "SIMPLE FIX: Discovery response sent (%d bytes) using same socket", sizeof(response));
        
        // Set GVSP client address for streaming
        gvsp_set_client_address(dest_addr);
        // Set client connected bit
        connection_status |= 0x04;
        return ESP_OK;
    }
}

static esp_err_t send_discovery_response(const gvcp_header_t *request_header, struct sockaddr_in *dest_addr, bool is_broadcast)
{
    // Create discovery ACK response with proper GigE Vision header
    // Use 8-byte GigE Vision header + bootstrap data
    uint8_t response[8 + GVBS_DISCOVERY_DATA_SIZE];
    
    // Determine packet ID
    uint16_t packet_id;
    if (request_header != NULL) {
        // CRITICAL: Solicited response - MUST echo back the EXACT same packet ID as request
        packet_id = ntohs(request_header->id);
        ESP_LOGI(TAG, "SOLICITED Response: echoing request ID=0x%04x back exactly", packet_id);
    } else {
        // Unsolicited broadcast - use current sequence number (incremented per packet)
        packet_id = discovery_broadcast_sequence & 0xFFFF;
        ESP_LOGI(TAG, "UNSOLICITED Broadcast: using sequence=%d as unique packet ID=0x%04x", 
                 discovery_broadcast_sequence, packet_id);
    }
    
    // Construct proper GigE Vision GVCP header (as suggested by user)
    response[0] = 0x42;                     // 'B' - GigE Vision magic byte 1
    response[1] = 0x45;                     // 'E' - GigE Vision magic byte 2
    response[2] = 0x81;                     // ACK + Big Endian flag
    response[3] = 0x02;                     // DISCOVERY_CMD 
    response[4] = 0x00;                     // status high byte
    response[5] = 0x00;                     // status low byte  
    response[6] = (packet_id >> 8) & 0xFF;  // packet ID high byte
    response[7] = packet_id & 0xFF;         // packet ID low byte
    
    // GVCP Header validation and debug logging
    ESP_LOGI(TAG, "GigE Vision GVCP Header: magic=%02x%02x, type=0x%02x, cmd=0x%02x, status=0x%04x, id=0x%04x",
             response[0], response[1], response[2], response[3], 
             (response[4] << 8) | response[5], (response[6] << 8) | response[7]);
    
    // Verify header structure integrity
    if (response[0] != 0x42 || response[1] != 0x45) {
        ESP_LOGE(TAG, "ERROR: Invalid magic bytes 0x%02x%02x, should be 0x4245", 
                 response[0], response[1]);
    }
    if (response[2] != 0x81) {
        ESP_LOGE(TAG, "ERROR: Invalid packet type 0x%02x, should be 0x81", response[2]);
    }
    if (response[3] != 0x02) {
        ESP_LOGE(TAG, "ERROR: Invalid command 0x%02x, should be 0x02 (DISCOVERY)", response[3]);
    }
    
    // Copy bootstrap data after the 8-byte GigE Vision header
    memcpy(&response[8], bootstrap_memory, GVBS_DISCOVERY_DATA_SIZE);
    
    // Simplified logging to reduce stack usage
    ESP_LOGI(TAG, "Sending discovery %s to %s:%d, ID:0x%04x", 
             is_broadcast ? "broadcast" : "response",
             inet_ntoa(dest_addr->sin_addr), ntohs(dest_addr->sin_port), packet_id);
    
    // Log the complete packet structure before transmission
    ESP_LOGI(TAG, "Complete packet: GigE header=8 bytes, payload=%d bytes, total=%d bytes",
             GVBS_DISCOVERY_DATA_SIZE, (int)sizeof(response));
    
    // Log first 16 bytes of packet for verification (should start with "42 45 81 02")
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, response, 16, ESP_LOG_INFO);
    
    // Send response
    esp_err_t err = gvcp_sendto(response, sizeof(response), dest_addr);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Discovery send failed, errno: %d", errno);
        return err;
    } else {
        ESP_LOGI(TAG, "Discovery %s sent successfully (%d bytes) with proper GigE Vision GVCP header (42 45 81 02)", 
                 is_broadcast ? "broadcast" : "response", (int)sizeof(response));
        
        if (!is_broadcast) {
            // Set GVSP client address for streaming (only for solicited responses)
            gvsp_set_client_address(dest_addr);
            // Set client connected bit
            connection_status |= 0x04;
        }
        return ESP_OK;
    }
}

static void handle_discovery_cmd(const gvcp_header_t *header, struct sockaddr_in *client_addr)
{
    uint16_t request_id = ntohs(header->id);
    ESP_LOGI(TAG, "Discovery SOLICITED from %s:%d, request ID:0x%04x (raw:0x%04x) - MUST echo back exactly", 
             inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), request_id, header->id);
    
    // PORT TRACKING: Log the client address before calling response function
    ESP_LOGI(TAG, "PORT TRACK 3: client_addr->sin_port = 0x%04x (%d) in handle_discovery_cmd", 
             client_addr->sin_port, ntohs(client_addr->sin_port));
    
    // EXPLICIT: Create and send discovery response with exact packet ID echo
    send_discovery_response_with_exact_id(header->id, client_addr);
}

static esp_err_t send_discovery_broadcast(void)
{
    if (!discovery_broadcast_enabled) {
        return ESP_OK;
    }
    
    // Send to multiple target addresses to ensure Aravis receives announcements
    const char* target_ips[] = {
        "192.168.213.45",  // Ethernet interface from Aravis log
        "192.168.213.28",  // WiFi interface from Aravis log  
        "192.168.213.255", // Network broadcast
        "255.255.255.255"  // Global broadcast
    };
    
    esp_err_t any_success = ESP_FAIL;
    uint32_t broadcast_cycle = discovery_broadcast_sequence + 1;
    
    for (int i = 0; i < 4; i++) {
        // Increment sequence for each individual broadcast packet to ensure unique packet IDs
        discovery_broadcast_sequence++;
        
        struct sockaddr_in target_addr;
        memset(&target_addr, 0, sizeof(target_addr));
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(GVCP_PORT);
        inet_pton(AF_INET, target_ips[i], &target_addr.sin_addr);
        
        ESP_LOGD(TAG, "Sending discovery announcement to %s (packet ID: %d)", target_ips[i], discovery_broadcast_sequence);
        
        // Try broadcast with retries for reliability
        esp_err_t result = ESP_FAIL;
        for (uint32_t retry = 0; retry < discovery_broadcast_retries; retry++) {
            result = send_discovery_response(NULL, &target_addr, true);
            if (result == ESP_OK) {
                any_success = ESP_OK;
                discovery_broadcasts_sent++;
                break;
            } else {
                if (retry < discovery_broadcast_retries - 1) {
                    vTaskDelay(pdMS_TO_TICKS(50)); // 50ms delay between retries
                }
            }
        }
        
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Discovery announcement to %s failed", target_ips[i]);
        }
    }
    
    if (any_success != ESP_OK) {
        discovery_broadcast_failures++;
        ESP_LOGE(TAG, "All discovery announcements failed for broadcast cycle #%d", broadcast_cycle);
    } else {
        ESP_LOGI(TAG, "Discovery announcements sent successfully (broadcast cycle #%d, packets %d-%d)", 
                 broadcast_cycle, broadcast_cycle, discovery_broadcast_sequence);
    }
    
    return any_success;
}

static void handle_read_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size < 8) {
        ESP_LOGE(TAG, "Invalid read memory command size: %d", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer in read memory command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    uint32_t size = ntohl(*(uint32_t*)(data + 4));
    
    ESP_LOGI(TAG, "Read memory: addr=0x%08x, size=%d", address, size);
    
    // Enhanced size validation
    if (size == 0) {
        ESP_LOGW(TAG, "Read memory command with zero size");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    // Allow larger reads for XML content at address 0x10000 and XML region
    uint32_t max_read_size;
    if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size) {
        max_read_size = 8192; // Large reads allowed for XML region
    } else {
        max_read_size = 512;  // Standard reads for other regions
    }
    
    if (size > max_read_size) {
        ESP_LOGW(TAG, "Read size %d exceeds maximum %d for address 0x%08x, clamping", 
                 size, max_read_size, address);
        size = max_read_size;
    }
    
    // Address alignment check for certain registers (4-byte aligned for 32-bit registers)
    bool is_register_access = (address >= GENICAM_ACQUISITION_START_OFFSET && 
                              address <= GENICAM_TRIGGER_MODE_OFFSET);
    if (is_register_access && (address % 4 != 0)) {
        ESP_LOGW(TAG, "Unaligned register access at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }
    
    // Create read memory ACK response - use dynamic allocation for large XML reads
    uint8_t *response;
    uint8_t stack_response[sizeof(gvcp_header_t) + 4 + 512];
    bool use_heap = (size > 512);
    
    if (use_heap) {
        response = malloc(sizeof(gvcp_header_t) + 4 + size);
        if (response == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for read memory response", sizeof(gvcp_header_t) + 4 + size);
            gvcp_send_nack(header, GVCP_ERROR_BUSY, client_addr);
            return;
        }
    } else {
        response = stack_response;
    }
    
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
    
    if (address < BOOTSTRAP_MEMORY_SIZE && address + size <= BOOTSTRAP_MEMORY_SIZE) {
        // Bootstrap memory region
        memcpy(data_ptr, &bootstrap_memory[address], size);
    } else if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size) {
        // GenICam XML memory region
        uint32_t xml_offset = address - XML_BASE_ADDRESS;
        uint32_t xml_read_size = size;
        
        ESP_LOGI(TAG, "XML read request: addr=0x%08x, offset=%d, requested_size=%d, xml_size=%d", 
                 address, xml_offset, size, genicam_xml_size);
        
        // Validate XML size
        if (genicam_xml_size == 0) {
            ESP_LOGE(TAG, "ERROR: genicam_xml_size is 0!");
            gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
            if (use_heap) free(response);
            return;
        }
        
        if (xml_offset + xml_read_size > genicam_xml_size) {
            xml_read_size = genicam_xml_size - xml_offset;
            ESP_LOGI(TAG, "XML read size clamped to %d bytes", xml_read_size);
        }
        
        if (xml_read_size > 0) {
            memcpy(data_ptr, &genicam_xml_data[xml_offset], xml_read_size);
            ESP_LOGI(TAG, "XML data copied: %d bytes from offset %d", xml_read_size, xml_offset);
            
            // Log first few bytes for debugging
            if (xml_read_size >= 16) {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, data_ptr, 16, ESP_LOG_INFO);
            }
        } else {
            ESP_LOGW(TAG, "XML read size is 0 after bounds checking");
        }
        
        // Fill remaining with zeros if needed
        if (xml_read_size < size) {
            memset(data_ptr + xml_read_size, 0, size - xml_read_size);
            ESP_LOGI(TAG, "Filled %d bytes with zeros after XML data", size - xml_read_size);
        }
    } else if (address == GENICAM_ACQUISITION_START_OFFSET && size >= 4) {
        // Acquisition start register
        uint32_t reg_value = htonl(acquisition_start_reg);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_ACQUISITION_STOP_OFFSET && size >= 4) {
        // Acquisition stop register
        uint32_t reg_value = htonl(acquisition_stop_reg);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_ACQUISITION_MODE_OFFSET && size >= 4) {
        // Acquisition mode register
        uint32_t reg_value = htonl(acquisition_mode);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_PIXEL_FORMAT_OFFSET && size >= 4) {
        // Pixel format register
        uint32_t reg_value = htonl(camera_get_genicam_pixformat());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_PACKET_DELAY_OFFSET && size >= 4) {
        // Packet delay register (microseconds)
        uint32_t reg_value = htonl(packet_delay_us);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_FRAME_RATE_OFFSET && size >= 4) {
        // Frame rate register (FPS)
        uint32_t reg_value = htonl(frame_rate_fps);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_PACKET_SIZE_OFFSET && size >= 4) {
        // Packet size register
        uint32_t reg_value = htonl(packet_size);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_STREAM_STATUS_OFFSET && size >= 4) {
        // Stream status register
        uint32_t reg_value = htonl(stream_status);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_PAYLOAD_SIZE_OFFSET && size >= 4) {
        // Payload size register (dynamic based on pixel format)
        uint32_t reg_value = htonl(camera_get_max_payload_size());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_JPEG_QUALITY_OFFSET && size >= 4) {
        // JPEG quality register
        uint32_t reg_value = htonl(camera_get_jpeg_quality());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_EXPOSURE_TIME_OFFSET && size >= 4) {
        // Exposure time register (microseconds)
        uint32_t reg_value = htonl(camera_get_exposure_time());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_GAIN_OFFSET && size >= 4) {
        // Gain register (dB)
        uint32_t reg_value = htonl((uint32_t)camera_get_gain());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_BRIGHTNESS_OFFSET && size >= 4) {
        // Brightness register (-2 to +2)
        uint32_t reg_value = htonl((uint32_t)camera_get_brightness());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_CONTRAST_OFFSET && size >= 4) {
        // Contrast register (-2 to +2)
        uint32_t reg_value = htonl((uint32_t)camera_get_contrast());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_SATURATION_OFFSET && size >= 4) {
        // Saturation register (-2 to +2)
        uint32_t reg_value = htonl((uint32_t)camera_get_saturation());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_WHITE_BALANCE_MODE_OFFSET && size >= 4) {
        // White balance mode register
        uint32_t reg_value = htonl((uint32_t)camera_get_white_balance_mode());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_TRIGGER_MODE_OFFSET && size >= 4) {
        // Trigger mode register
        uint32_t reg_value = htonl((uint32_t)camera_get_trigger_mode());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_TOTAL_COMMANDS_OFFSET && size >= 4) {
        // Total commands received register
        uint32_t reg_value = htonl(total_commands_received);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_TOTAL_ERRORS_OFFSET && size >= 4) {
        // Total errors sent register
        uint32_t reg_value = htonl(total_errors_sent);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_UNKNOWN_COMMANDS_OFFSET && size >= 4) {
        // Unknown commands register
        uint32_t reg_value = htonl(total_unknown_commands);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_PACKETS_SENT_OFFSET && size >= 4) {
        // Total packets sent register (from GVSP)
        uint32_t reg_value = htonl(gvsp_get_total_packets_sent());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_PACKET_ERRORS_OFFSET && size >= 4) {
        // Packet errors register (from GVSP)
        uint32_t reg_value = htonl(gvsp_get_total_packet_errors());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_FRAMES_SENT_OFFSET && size >= 4) {
        // Total frames sent register (from GVSP)
        uint32_t reg_value = htonl(gvsp_get_total_frames_sent());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_FRAME_ERRORS_OFFSET && size >= 4) {
        // Frame errors register (from GVSP)
        uint32_t reg_value = htonl(gvsp_get_total_frame_errors());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_CONNECTION_STATUS_OFFSET && size >= 4) {
        // Connection status register
        uint32_t reg_value = htonl(connection_status);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_OUT_OF_ORDER_FRAMES_OFFSET && size >= 4) {
        // Out-of-order frames register
        uint32_t reg_value = htonl(gvsp_get_out_of_order_frames());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_LOST_FRAMES_OFFSET && size >= 4) {
        // Lost frames register
        uint32_t reg_value = htonl(gvsp_get_lost_frames());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_DUPLICATE_FRAMES_OFFSET && size >= 4) {
        // Duplicate frames register
        uint32_t reg_value = htonl(gvsp_get_duplicate_frames());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_EXPECTED_SEQUENCE_OFFSET && size >= 4) {
        // Expected sequence register
        uint32_t reg_value = htonl(gvsp_get_expected_frame_sequence());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_LAST_SEQUENCE_OFFSET && size >= 4) {
        // Last received sequence register
        uint32_t reg_value = htonl(gvsp_get_last_received_sequence());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_FRAMES_IN_RING_OFFSET && size >= 4) {
        // Frames in ring buffer register
        uint32_t reg_value = htonl(gvsp_get_frames_stored_in_ring());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_CONNECTION_FAILURES_OFFSET && size >= 4) {
        // Connection failures register
        uint32_t reg_value = htonl(gvsp_get_connection_failures());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_RECOVERY_MODE_OFFSET && size >= 4) {
        // Recovery mode register (0 = false, 1 = true)
        uint32_t reg_value = htonl(gvsp_is_in_recovery_mode() ? 1 : 0);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET && size >= 4) {
        // Discovery broadcast enable register (0 = disabled, 1 = enabled)
        uint32_t reg_value = htonl(discovery_broadcast_enabled ? 1 : 0);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET && size >= 4) {
        // Discovery broadcast interval register (milliseconds)
        uint32_t reg_value = htonl(discovery_broadcast_interval_ms);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_DISCOVERY_BROADCASTS_SENT_OFFSET && size >= 4) {
        // Discovery broadcasts sent register
        uint32_t reg_value = htonl(discovery_broadcasts_sent);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_DISCOVERY_BROADCAST_FAILURES_OFFSET && size >= 4) {
        // Discovery broadcast failures register
        uint32_t reg_value = htonl(discovery_broadcast_failures);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET && size >= 4) {
        // Discovery broadcast sequence register
        uint32_t reg_value = htonl(discovery_broadcast_sequence);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else {
        // Unknown memory region - return zeros
        memset(data_ptr, 0, size);
    }
    
    // Send response
    size_t response_size = sizeof(gvcp_header_t) + 4 + size;
    esp_err_t err = gvcp_sendto(response, response_size, client_addr);
    
    // Cleanup heap allocation if used
    if (use_heap) {
        free(response);
    }
    
    // Update client activity
    gvsp_update_client_activity();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending read memory ACK");
    } else {
        ESP_LOGI(TAG, "Sent read memory ACK (%d bytes)", response_size);
    }
}

static void handle_write_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size < 4) {
        ESP_LOGE(TAG, "Invalid write memory command size: %d", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer in write memory command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    uint32_t size = packet_size - 4;
    const uint8_t *write_data = data + 4;
    
    ESP_LOGI(TAG, "Write memory: addr=0x%08x, size=%d", address, size);
    
    // Enhanced write validation
    if (size == 0) {
        ESP_LOGW(TAG, "Write memory command with zero size");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (size > 512) {
        ESP_LOGW(TAG, "Write size %d exceeds maximum", size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    // Address alignment check for register access
    bool is_register_access = (address >= GENICAM_ACQUISITION_START_OFFSET && 
                              address <= GENICAM_JPEG_QUALITY_OFFSET);
    if (is_register_access && (address % 4 != 0)) {
        ESP_LOGW(TAG, "Unaligned register write at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }
    
    // Handle different write regions
    
    if (address >= GVBS_USER_DEFINED_NAME_OFFSET && 
        address + size <= GVBS_USER_DEFINED_NAME_OFFSET + 16) {
        // Bootstrap user-defined name
        memcpy(&bootstrap_memory[address], write_data, size);
        ESP_LOGI(TAG, "Write successful to bootstrap address 0x%08x", address);
    } else if (address == GENICAM_ACQUISITION_START_OFFSET && size >= 4) {
        // Acquisition start command
        uint32_t command_value = ntohl(*(uint32_t*)write_data);
        if (command_value == 1) {
            ESP_LOGI(TAG, "Acquisition start command received");
            status_led_set_state(LED_STATE_FAST_BLINK);
            gvsp_start_streaming();
            acquisition_start_reg = 1;
            // Set streaming active bit
            connection_status |= 0x08;
        }
    } else if (address == GENICAM_ACQUISITION_STOP_OFFSET && size >= 4) {
        // Acquisition stop command
        uint32_t command_value = ntohl(*(uint32_t*)write_data);
        if (command_value == 1) {
            ESP_LOGI(TAG, "Acquisition stop command received");
            status_led_set_state(LED_STATE_ON);
            gvsp_stop_streaming();
            gvsp_clear_client_address();
            acquisition_stop_reg = 1;
            // Clear streaming active bit and client connected bit
            connection_status &= ~(0x08 | 0x04);
        }
    } else if (address == GENICAM_ACQUISITION_MODE_OFFSET && size >= 4) {
        // Acquisition mode
        acquisition_mode = ntohl(*(uint32_t*)write_data);
        ESP_LOGI(TAG, "Acquisition mode set to: %d", acquisition_mode);
    } else if (address == GENICAM_PIXEL_FORMAT_OFFSET && size >= 4) {
        // Pixel format
        uint32_t format_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_genicam_pixformat(format_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Pixel format set to: 0x%08X", format_value);
            } else {
            ESP_LOGW(TAG, "Failed to set pixel format: 0x%08X", format_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_PACKET_DELAY_OFFSET && size >= 4) {
        // Packet delay (microseconds)
        uint32_t new_delay = ntohl(*(uint32_t*)write_data);
        if (new_delay >= 100 && new_delay <= 100000) { // 0.1ms to 100ms range
            packet_delay_us = new_delay;
            ESP_LOGI(TAG, "Packet delay set to: %d microseconds", packet_delay_us);
            } else {
            ESP_LOGW(TAG, "Packet delay %d out of range (100-100000 us)", new_delay);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_FRAME_RATE_OFFSET && size >= 4) {
        // Frame rate (FPS)
        uint32_t new_fps = ntohl(*(uint32_t*)write_data);
        if (new_fps >= 1 && new_fps <= 30) { // 1-30 FPS range
            frame_rate_fps = new_fps;
            ESP_LOGI(TAG, "Frame rate set to: %d FPS", frame_rate_fps);
            } else {
            ESP_LOGW(TAG, "Frame rate %d out of range (1-30 FPS)", new_fps);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_PACKET_SIZE_OFFSET && size >= 4) {
        // Packet size
        uint32_t new_size = ntohl(*(uint32_t*)write_data);
        if (new_size >= 512 && new_size <= GVSP_DATA_PACKET_SIZE) {
            packet_size = new_size;
            ESP_LOGI(TAG, "Packet size set to: %d bytes", packet_size);
            } else {
            ESP_LOGW(TAG, "Packet size %d out of range (512-%d bytes)", new_size, GVSP_DATA_PACKET_SIZE);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_JPEG_QUALITY_OFFSET && size >= 4) {
        // JPEG quality
        uint32_t quality_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_jpeg_quality(quality_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "JPEG quality set to: %d", quality_value);
            } else {
            ESP_LOGW(TAG, "Failed to set JPEG quality: %d", quality_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_EXPOSURE_TIME_OFFSET && size >= 4) {
        // Exposure time
        uint32_t exposure_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_exposure_time(exposure_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Exposure time set to: %lu us", exposure_value);
            } else {
            ESP_LOGW(TAG, "Failed to set exposure time: %lu us", exposure_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_GAIN_OFFSET && size >= 4) {
        // Gain
        uint32_t gain_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_gain((int)gain_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Gain set to: %d dB", (int)gain_value);
            } else {
            ESP_LOGW(TAG, "Failed to set gain: %d dB", (int)gain_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_BRIGHTNESS_OFFSET && size >= 4) {
        // Brightness
        int32_t brightness_value = (int32_t)ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_brightness(brightness_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Brightness set to: %d", brightness_value);
            } else {
            ESP_LOGW(TAG, "Failed to set brightness: %d", brightness_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_CONTRAST_OFFSET && size >= 4) {
        // Contrast
        int32_t contrast_value = (int32_t)ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_contrast(contrast_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Contrast set to: %d", contrast_value);
            } else {
            ESP_LOGW(TAG, "Failed to set contrast: %d", contrast_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_SATURATION_OFFSET && size >= 4) {
        // Saturation
        int32_t saturation_value = (int32_t)ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_saturation(saturation_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Saturation set to: %d", saturation_value);
            } else {
            ESP_LOGW(TAG, "Failed to set saturation: %d", saturation_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_WHITE_BALANCE_MODE_OFFSET && size >= 4) {
        // White balance mode
        uint32_t wb_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_white_balance_mode((int)wb_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "White balance mode set to: %s", wb_value == 0 ? "OFF" : "AUTO");
            } else {
            ESP_LOGW(TAG, "Failed to set white balance mode: %d", (int)wb_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_TRIGGER_MODE_OFFSET && size >= 4) {
        // Trigger mode
        uint32_t trigger_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_trigger_mode((int)trigger_value);
        if (ret == ESP_OK) {
            const char* mode_str = trigger_value == 0 ? "OFF" : 
                                  trigger_value == 1 ? "ON" : "SOFTWARE";
            ESP_LOGI(TAG, "Trigger mode set to: %s", mode_str);
            } else {
            ESP_LOGW(TAG, "Failed to set trigger mode: %d", (int)trigger_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET && size >= 4) {
        // Discovery broadcast enable/disable
        uint32_t enable_value = ntohl(*(uint32_t*)write_data);
        bool enable = (enable_value != 0);
        gvcp_enable_discovery_broadcast(enable);
        ESP_LOGI(TAG, "Discovery broadcast %s", enable ? "enabled" : "disabled");
    } else if (address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET && size >= 4) {
        // Discovery broadcast interval
        uint32_t interval_value = ntohl(*(uint32_t*)write_data);
        gvcp_set_discovery_broadcast_interval(interval_value);
        ESP_LOGI(TAG, "Discovery broadcast interval set to %d ms", interval_value);
    } else {
        ESP_LOGW(TAG, "Write denied to address 0x%08x (read-only or out of range)", address);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
        return;
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
    esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);
    
    // Update client activity
    gvsp_update_client_activity();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending write memory ACK");
    } else {
        ESP_LOGI(TAG, "Sent write memory ACK");
    }
}

static void handle_gvcp_packet(const uint8_t *packet, int len, struct sockaddr_in *client_addr)
{
    total_commands_received++;
    
    // PORT TRACKING: Log the client address at entry
    ESP_LOGI(TAG, "PORT TRACK 2: client_addr->sin_port = 0x%04x (%d) in handle_gvcp_packet", 
             client_addr->sin_port, ntohs(client_addr->sin_port));
    
    // Enhanced packet validation
    if (len < sizeof(gvcp_header_t)) {
        ESP_LOGE(TAG, "Packet too small for GVCP header: %d bytes", len);
        return;
    }
    
    if (packet == NULL || client_addr == NULL) {
        ESP_LOGE(TAG, "NULL pointer in packet handler");
        return;
    }
    
    const gvcp_header_t *header = (const gvcp_header_t*)packet;
    const uint8_t *data = packet + sizeof(gvcp_header_t);
    
    // Validate packet type
    if (header->packet_type != GVCP_PACKET_TYPE_CMD) {
        ESP_LOGW(TAG, "Invalid packet type: 0x%02x", header->packet_type);
        return;
    }
    
    uint16_t command = ntohs(header->command);
    uint16_t packet_id = ntohs(header->id);
    uint16_t payload_size = ntohs(header->size);
    
    // Validate payload size
    if (len < sizeof(gvcp_header_t) + payload_size) {
        ESP_LOGW(TAG, "Packet too small for declared payload: %d < %d", 
                 len, sizeof(gvcp_header_t) + payload_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_HEADER, client_addr);
        return;
    }
    
    ESP_LOGI(TAG, "GVCP packet: type=0x%02x, cmd=0x%04x, id=0x%04x, size=%d",
             header->packet_type, command, packet_id, payload_size);
    
    // Extra debug for discovery commands to track the packet ID issue
    if (command == GVCP_CMD_DISCOVERY) {
        ESP_LOGI(TAG, "DISCOVERY command received with packet ID=0x%04x, header raw ID=0x%04x",
                 packet_id, header->id);
        
        // Debug the raw packet bytes for packet ID (bytes 6-7)
        const uint8_t *raw_packet = (const uint8_t*)header;
        ESP_LOGI(TAG, "Raw packet ID bytes: [6]=0x%02x [7]=0x%02x, combined=0x%04x", 
                 raw_packet[6], raw_packet[7], (raw_packet[6] << 8) | raw_packet[7]);
    }
    
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
            ESP_LOGW(TAG, "Unknown GVCP command: 0x%04x", command);
            total_unknown_commands++;
            gvcp_send_nack(header, GVCP_ERROR_NOT_IMPLEMENTED, client_addr);
            break;
    }
}

void gvcp_task(void *pvParameters)
{
    char rx_buffer[1024];
    char addr_str[128];

    ESP_LOGI(TAG, "GVCP task started");

    // Add this task to watchdog monitoring
    esp_task_wdt_add(NULL);

    while (1) {
        // Feed the watchdog 
        esp_task_wdt_reset();
        
        // Check if it's time to send discovery broadcast
        uint32_t current_time = esp_log_timestamp();
        if (discovery_broadcast_enabled && sock >= 0 &&
            (current_time - last_discovery_broadcast_time >= discovery_broadcast_interval_ms)) {
            
            ESP_LOGI(TAG, "Sending periodic discovery broadcast (sequence #%d)", discovery_broadcast_sequence + 1);
            esp_err_t broadcast_result = send_discovery_broadcast();
            if (broadcast_result == ESP_OK) {
                last_discovery_broadcast_time = current_time;
                ESP_LOGI(TAG, "Discovery broadcast sent successfully");
            } else {
                ESP_LOGW(TAG, "Discovery broadcast failed, will retry next interval");
            }
        }
        
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            // Handle timeout separately from errors
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket timeout - this is normal, just continue
                // Periodically log that we're still listening
                static uint32_t timeout_count = 0;
                timeout_count++;
                if (timeout_count % 50 == 0) {  // Log every 25 seconds (500ms * 50)
                    ESP_LOGD(TAG, "GVCP listening (timeout %d, socket healthy)", timeout_count);
                }
                continue;
            }
            
            ESP_LOGE(TAG, "recvfrom failed: errno %d (%s)", errno, strerror(errno));
            gvcp_socket_error_count++;
            
            // Check for specific network errors that indicate socket issues
            if (errno == EBADF || errno == ENOTSOCK || errno == ENETDOWN || errno == ENETUNREACH) {
                ESP_LOGW(TAG, "Network/socket error detected in GVCP: errno %d", errno);
                
                // Trigger socket recreation if we've had enough errors
                if (gvcp_socket_error_count >= gvcp_max_socket_errors) {
                    ESP_LOGW(TAG, "Max GVCP socket errors reached (%d), attempting socket recreation", 
                             gvcp_socket_error_count);
                    esp_err_t recreate_result = gvcp_recreate_socket();
                    if (recreate_result != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to recreate GVCP socket, will retry later");
                        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before continuing
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second before retry
                }
            } else {
                // Other errors, just wait a bit
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else if (len > 0) {
            // Reset socket error count on successful receive
            if (gvcp_socket_error_count > 0) {
                gvcp_socket_error_count = 0;
            }
            inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
            
            ESP_LOGI(TAG, "Received %d bytes from %s:%d", len, addr_str, ntohs(source_addr.sin_port));
            ESP_LOGI(TAG, "PORT TRACK 1: source_addr.sin_port = 0x%04x (%d) after recvfrom", 
                     source_addr.sin_port, ntohs(source_addr.sin_port));
            
            // Simplified discovery packet logging
            if (len == 8) {
                const gvcp_header_t *debug_header = (const gvcp_header_t*)rx_buffer;
                uint16_t debug_command = ntohs(debug_header->command);
                
                if (debug_command == GVCP_CMD_DISCOVERY) {
                    ESP_LOGI(TAG, "Discovery packet from %s:%d, ID:0x%04x", 
                             addr_str, ntohs(source_addr.sin_port), ntohs(debug_header->id));
                }
            }
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

// Stream configuration getter functions
uint32_t gvcp_get_packet_delay_us(void)
{
    return packet_delay_us;
}

uint32_t gvcp_get_frame_rate_fps(void)
{
    return frame_rate_fps;
}

uint32_t gvcp_get_packet_size(void)
{
    return packet_size;
}

void gvcp_set_stream_status(uint32_t status)
{
    stream_status = status;
}

// Error statistics functions
uint32_t gvcp_get_total_commands_received(void)
{
    return total_commands_received;
}

uint32_t gvcp_get_total_errors_sent(void)
{
    return total_errors_sent;
}

uint32_t gvcp_get_total_unknown_commands(void)
{
    return total_unknown_commands;
}

// Connection status management
void gvcp_set_connection_status_bit(uint8_t bit_position, bool value)
{
    if (bit_position < 32) {
        if (value) {
            connection_status |= (1U << bit_position);
        } else {
            connection_status &= ~(1U << bit_position);
        }
    }
}

uint32_t gvcp_get_connection_status(void)
{
    return connection_status;
}

// Discovery broadcast management functions
void gvcp_enable_discovery_broadcast(bool enable)
{
    discovery_broadcast_enabled = enable;
    ESP_LOGI(TAG, "Discovery broadcast %s", enable ? "enabled" : "disabled");
}

void gvcp_set_discovery_broadcast_interval(uint32_t interval_ms)
{
    if (interval_ms >= 1000 && interval_ms <= 30000) {
        discovery_broadcast_interval_ms = interval_ms;
        ESP_LOGI(TAG, "Discovery broadcast interval set to %d ms", interval_ms);
    } else {
        ESP_LOGW(TAG, "Invalid broadcast interval %d ms, keeping current %d ms", 
                 interval_ms, discovery_broadcast_interval_ms);
    }
}

esp_err_t gvcp_trigger_discovery_broadcast(void)
{
    if (!discovery_broadcast_enabled) {
        ESP_LOGW(TAG, "Discovery broadcast is disabled");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (sock < 0) {
        ESP_LOGW(TAG, "GVCP socket not yet initialized, discovery broadcast deferred");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Triggering immediate discovery broadcast");
    esp_err_t result = send_discovery_broadcast();
    if (result == ESP_OK) {
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