#include "gvcp_bootstrap.h"
#include "gvcp_discovery.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_chip_info.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "gvcp_bootstrap";

static uint8_t bootstrap_memory[BOOTSTRAP_MEMORY_SIZE];

// Helper function to write register values with proper byte order
static void write_register_value(uint8_t *dest, uint32_t value, size_t size)
{
    uint32_t val_net = htonl(value);
    memcpy(dest, &val_net, 4);
    if (size > 4)
    {
        memset(dest + 4, 0, size - 4);
    }
}

// Control Channel Privilege registers
static uint32_t control_channel_privilege = 0;     // Current privilege level (bitfield)
static uint32_t control_channel_privilege_key = 0; // Privilege key register

// Control Channel Privilege validation function
bool is_valid_privilege_value(uint32_t value)
{
    // According to GigE Vision specification, CCP register uses bitfields:
    // 0x00000000 - No access
    // 0x00000001 - Exclusive control (bit 0)
    // 0x00000200 - Primary control (bit 9) - used by Aravis and other tools
    // 0x00000201 - Both exclusive and primary (some clients)

    if (value == 0x00000000 || // No access
        value == 0x00000001 || // Exclusive control
        value == 0x00000200 || // Primary control
        value == 0x00000201)
    { // Both exclusive and primary
        return true;
    }

    ESP_LOGW(TAG, "Invalid privilege value 0x%08x requested", value);
    return false;
}

void init_bootstrap_memory(void)
{
    memset(bootstrap_memory, 0, sizeof(bootstrap_memory));

    // Version register (Major=1, Minor=0)
    write_register_value(&bootstrap_memory[GVBS_VERSION_OFFSET], 0x00010000, 4);

    // Device mode (big endian, UTF8)
    write_register_value(&bootstrap_memory[GVBS_DEVICE_MODE_OFFSET], 0x80000000, 4);

    // Device capabilities register (indicate GigE Vision support)
    write_register_value(&bootstrap_memory[GVBS_DEVICE_CAPABILITIES_OFFSET], 0x00000001, 4); // Bit 0: GigE Vision supported

    // Get MAC address - encode according to GigE Vision spec
    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    if (ret == ESP_OK)
    {
        // GigE Vision MAC address format: high = first 2 bytes, low = last 4 bytes
        // Store in network byte order (big endian)
        write_register_value(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_HIGH_OFFSET], (uint32_t)(mac[0] << 8) | mac[1], 4);
        write_register_value(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_LOW_OFFSET], (uint32_t)(mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5], 4);

        // Generate and store 128-bit unique device UUID
        uint8_t device_uuid[16];
        generate_device_uuid(device_uuid, mac, DEVICE_SERIAL);
        memcpy(&bootstrap_memory[GVBS_DEVICE_UUID_OFFSET], device_uuid, 16);
    }

    // Get current IP address and network configuration
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif)
    {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
        {
            uint32_t ip = ip_info.ip.addr;
            uint32_t netmask = ip_info.netmask.addr;
            uint32_t gateway = ip_info.gw.addr;

            // IP addresses from esp_netif are already in network byte order (big-endian)
            // Use direct memcpy to avoid double conversion by write_register_value()
            memcpy(&bootstrap_memory[GVBS_CURRENT_IP_ADDRESS_OFFSET], &ip, 4);
            memcpy(&bootstrap_memory[GVBS_CURRENT_SUBNET_MASK_OFFSET], &netmask, 4);
            memcpy(&bootstrap_memory[GVBS_CURRENT_DEFAULT_GATEWAY_OFFSET], &gateway, 4);

            // Supported IP configuration register (static IP, DHCP, etc.)
            // Bit 0: Manual IP, Bit 1: DHCP, Bit 2: AutoIP, Bit 3: Persistent IP
            write_register_value(&bootstrap_memory[GVBS_SUPPORTED_IP_CONFIG_OFFSET], 0x00000006, 4); // DHCP + AutoIP supported

            // Current IP configuration register (which method is currently active)
            write_register_value(&bootstrap_memory[GVBS_CURRENT_IP_CONFIG_OFFSET], 0x00000002, 4); // DHCP currently active

            // Link speed register (WiFi typically 54 Mbps for 802.11g, 150+ for 802.11n)
            write_register_value(&bootstrap_memory[GVBS_LINK_SPEED_OFFSET], 54000000, 4); // 54 Mbps in bps
        }
    }

    // Device strings (ensure proper null termination)
    memset(&bootstrap_memory[GVBS_MANUFACTURER_NAME_OFFSET], 0, 32);
    memset(&bootstrap_memory[GVBS_MODEL_NAME_OFFSET], 0, 32);
    memset(&bootstrap_memory[GVBS_DEVICE_VERSION_OFFSET], 0, 32);
    memset(&bootstrap_memory[GVBS_SERIAL_NUMBER_OFFSET], 0, 16);
    memset(&bootstrap_memory[GVBS_USER_DEFINED_NAME_OFFSET], 0, 16);

    strncpy((char *)&bootstrap_memory[GVBS_MANUFACTURER_NAME_OFFSET], DEVICE_MANUFACTURER, 31);
    strncpy((char *)&bootstrap_memory[GVBS_MODEL_NAME_OFFSET], DEVICE_MODEL, 31);
    strncpy((char *)&bootstrap_memory[GVBS_DEVICE_VERSION_OFFSET], DEVICE_VERSION, 31);
    strncpy((char *)&bootstrap_memory[GVBS_SERIAL_NUMBER_OFFSET], DEVICE_SERIAL, 15);
    strncpy((char *)&bootstrap_memory[GVBS_USER_DEFINED_NAME_OFFSET], DEVICE_USER_NAME, 15);

    // Control Channel Privilege registers (initialize to no access)
    write_register_value(&bootstrap_memory[GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET], 0, 4); // 0 = No access

    write_register_value(&bootstrap_memory[GVBS_CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET], 0, 4); // Key register initialized to 0

    // XML URL pointer register (points to the XML URL string location)
    write_register_value(&bootstrap_memory[GVBS_XML_URL_POINTER_OFFSET], GVBS_XML_URL_0_OFFSET, 4);

    // XML URL (safe size: BOOTSTRAP_MEMORY_SIZE - GVBS_XML_URL_0_OFFSET)
    size_t xml_url_max_size = BOOTSTRAP_MEMORY_SIZE - GVBS_XML_URL_0_OFFSET;
    strncpy((char *)&bootstrap_memory[GVBS_XML_URL_0_OFFSET], XML_URL, xml_url_max_size - 1);
    bootstrap_memory[GVBS_XML_URL_0_OFFSET + xml_url_max_size - 1] = '\0'; // Ensure null termination

    // Aravis fallback XML URL addresses
    snprintf((char *)&bootstrap_memory[0x400], BOOTSTRAP_MEMORY_SIZE - 0x400, XML_URL);

    // Heartbeat timeout register (ms) - default 3000ms (3 seconds)
    // This register tells Aravis how often to send heartbeat messages
    write_register_value(&bootstrap_memory[GVBS_HEARTBEAT_TIMEOUT_OFFSET], 3000, 4);

    ESP_LOGI(TAG, "Bootstrap memory initialized with heartbeat timeout 3000ms");
}

uint8_t *get_bootstrap_memory(void)
{
    return bootstrap_memory;
}

size_t get_bootstrap_memory_size(void)
{
    return BOOTSTRAP_MEMORY_SIZE;
}

uint32_t gvcp_get_control_channel_privilege(void)
{
    return control_channel_privilege;
}

void gvcp_set_control_channel_privilege(uint32_t value)
{
    if (is_valid_privilege_value(value))
    {
        control_channel_privilege = value;
        ESP_LOGI(TAG, "Control channel privilege set to 0x%08x", value);
    }
    else
    {
        ESP_LOGW(TAG, "Rejected invalid privilege value 0x%08x", value);
    }
}

uint32_t gvcp_get_control_channel_privilege_key(void)
{
    return control_channel_privilege_key;
}

void gvcp_set_control_channel_privilege_key(uint32_t value)
{
    control_channel_privilege_key = value;
    ESP_LOGI(TAG, "Control channel privilege key set to 0x%08x", value);
}

esp_err_t gvcp_bootstrap_init(void)
{
    // Initialize privilege registers
    control_channel_privilege = 0;
    control_channel_privilege_key = 0;

    // Initialize bootstrap memory
    init_bootstrap_memory();

    ESP_LOGI(TAG, "Bootstrap registers initialized");
    return ESP_OK;
}