#include "bootstrap.h"
#include "../utils/platform.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "gvcp_bootstrap";

static uint8_t bootstrap_memory[BOOTSTRAP_MEMORY_SIZE];

// Control Channel Privilege registers
static uint32_t control_channel_privilege = 0;     // Current privilege level (bitfield)
static uint32_t control_channel_privilege_key = 0; // Privilege key register

// Network info storage
static gvcp_network_info_t network_info = {0};

// Simple hash function for UUID generation (platform-independent)
static uint32_t simple_hash(const uint8_t *data, size_t len, uint32_t seed) {
    uint32_t hash = seed;
    for (size_t i = 0; i < len; i++) {
        hash = hash * 31 + data[i];
        hash ^= hash >> 16;
    }
    return hash;
}

// Helper function to write register values with proper byte order
static void write_register_value(uint8_t *dest, uint32_t value, size_t size) {
    uint32_t val_net = htonl(value);
    memcpy(dest, &val_net, 4);
    if (size > 4) {
        memset(dest + 4, 0, size - 4);
    }
}

// Control Channel Privilege validation function
bool gvcp_bootstrap_is_valid_privilege_value(uint32_t value) {
    // According to GigE Vision specification, CCP register uses bitfields:
    // 0x00000000 - No access
    // 0x00000001 - Exclusive control (bit 0)
    // 0x00000200 - Primary control (bit 9) - used by Aravis and other tools
    // 0x00000201 - Both exclusive and primary (some clients)

    if (value == 0x00000000 || // No access
        value == 0x00000001 || // Exclusive control
        value == 0x00000200 || // Primary control
        value == 0x00000201) { // Both exclusive and primary
        return true;
    }

    platform->log_warn(TAG, "Invalid privilege value 0x%08x requested", value);
    return false;
}

void gvcp_bootstrap_generate_device_uuid(uint8_t *uuid_out, const uint8_t *mac, const char *serial_number) {
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
    
    // Add serial number for additional uniqueness
    if (serial_number) {
        size_t serial_len = strlen(serial_number);
        if (serial_len > 16)
            serial_len = 16;
        memcpy(&input_buffer[offset], serial_number, serial_len);
        offset += serial_len;
    }
    
    // Generate 128-bit UUID using 4 different hash seeds
    uint32_t *uuid_words = (uint32_t *)uuid_out;
    uuid_words[0] = htonl(simple_hash(input_buffer, offset, 0x12345678));
    uuid_words[1] = htonl(simple_hash(input_buffer, offset, 0x9ABCDEF0));
    uuid_words[2] = htonl(simple_hash(input_buffer, offset, 0xFEDCBA98));
    uuid_words[3] = htonl(simple_hash(input_buffer, offset, 0x76543210));
    
    platform->log_info(TAG, "Generated device UUID from MAC + model + version + serial");
}

static void init_bootstrap_memory(void) {
    memset(bootstrap_memory, 0, sizeof(bootstrap_memory));

    // Version register (Major=1, Minor=0)
    write_register_value(&bootstrap_memory[GVBS_VERSION_OFFSET], 0x00010000, 4);

    // Device mode (big endian, UTF8)
    write_register_value(&bootstrap_memory[GVBS_DEVICE_MODE_OFFSET], 0x80000000, 4);

    // Device capabilities register (indicate GigE Vision support)
    write_register_value(&bootstrap_memory[GVBS_DEVICE_CAPABILITIES_OFFSET], 0x00000001, 4); // Bit 0: GigE Vision supported

    // Handle network information if available
    if (network_info.has_network_info) {
        // MAC address format: high = first 2 bytes, low = last 4 bytes
        // Store in network byte order (big endian)
        write_register_value(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_HIGH_OFFSET], 
                           (uint32_t)(network_info.mac_address[0] << 8) | network_info.mac_address[1], 4);
        write_register_value(&bootstrap_memory[GVBS_DEVICE_MAC_ADDRESS_LOW_OFFSET], 
                           (uint32_t)(network_info.mac_address[2] << 24) | (network_info.mac_address[3] << 16) | 
                           (network_info.mac_address[4] << 8) | network_info.mac_address[5], 4);

        // Generate and store 128-bit unique device UUID
        uint8_t device_uuid[16];
        gvcp_bootstrap_generate_device_uuid(device_uuid, network_info.mac_address, DEVICE_SERIAL);
        memcpy(&bootstrap_memory[GVBS_DEVICE_UUID_OFFSET], device_uuid, 16);

        // IP addresses are already in network byte order (big-endian)
        // Use direct memcpy to avoid double conversion by write_register_value()
        memcpy(&bootstrap_memory[GVBS_CURRENT_IP_ADDRESS_OFFSET], &network_info.ip_address, 4);
        memcpy(&bootstrap_memory[GVBS_CURRENT_SUBNET_MASK_OFFSET], &network_info.subnet_mask, 4);
        memcpy(&bootstrap_memory[GVBS_CURRENT_DEFAULT_GATEWAY_OFFSET], &network_info.gateway, 4);

        // Supported IP configuration register (static IP, DHCP, etc.)
        // Bit 0: Manual IP, Bit 1: DHCP, Bit 2: AutoIP, Bit 3: Persistent IP
        write_register_value(&bootstrap_memory[GVBS_SUPPORTED_IP_CONFIG_OFFSET], 0x00000006, 4); // DHCP + AutoIP supported

        // Current IP configuration register (which method is currently active)
        write_register_value(&bootstrap_memory[GVBS_CURRENT_IP_CONFIG_OFFSET], 0x00000002, 4); // DHCP currently active

        // Link speed register (WiFi typically 54 Mbps for 802.11g, 150+ for 802.11n)
        write_register_value(&bootstrap_memory[GVBS_LINK_SPEED_OFFSET], 54000000, 4); // 54 Mbps in bps
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

    // Heartbeat timeout register (ms) - default 3000ms (3 seconds)
    // This register tells Aravis how often to send heartbeat messages
    write_register_value(&bootstrap_memory[GVBS_HEARTBEAT_TIMEOUT_OFFSET], 3000, 4);

    platform->log_info(TAG, "Bootstrap memory initialized with heartbeat timeout 3000ms");
}

void gvcp_bootstrap_set_network_info(const gvcp_network_info_t *net_info) {
    if (net_info) {
        network_info = *net_info;
        platform->log_info(TAG, "Network info updated");
    }
}

uint8_t *gvcp_bootstrap_get_memory(void) {
    return bootstrap_memory;
}

size_t gvcp_bootstrap_get_memory_size(void) {
    return BOOTSTRAP_MEMORY_SIZE;
}

uint32_t gvcp_bootstrap_get_control_channel_privilege(void) {
    return control_channel_privilege;
}

void gvcp_bootstrap_set_control_channel_privilege(uint32_t value) {
    if (gvcp_bootstrap_is_valid_privilege_value(value)) {
        control_channel_privilege = value;
        platform->log_info(TAG, "Control channel privilege set to 0x%08x", value);
    } else {
        platform->log_warn(TAG, "Rejected invalid privilege value 0x%08x", value);
    }
}

uint32_t gvcp_bootstrap_get_control_channel_privilege_key(void) {
    return control_channel_privilege_key;
}

void gvcp_bootstrap_set_control_channel_privilege_key(uint32_t value) {
    control_channel_privilege_key = value;
    platform->log_info(TAG, "Control channel privilege key set to 0x%08x", value);
}

gvcp_bootstrap_result_t gvcp_bootstrap_init(void) {
    // Initialize privilege registers
    control_channel_privilege = 0;
    control_channel_privilege_key = 0;

    // Initialize bootstrap memory
    init_bootstrap_memory();

    platform->log_info(TAG, "Bootstrap registers initialized");
    return GVCP_BOOTSTRAP_SUCCESS;
}