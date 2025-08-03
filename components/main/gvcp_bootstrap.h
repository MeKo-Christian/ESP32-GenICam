#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Bootstrap register offsets (from Aravis GVBS definitions)
#define GVBS_VERSION_OFFSET 0x00000000
#define GVBS_DEVICE_MODE_OFFSET 0x00000004
#define GVBS_DEVICE_MAC_ADDRESS_HIGH_OFFSET 0x00000008
#define GVBS_DEVICE_MAC_ADDRESS_LOW_OFFSET 0x0000000c
#define GVBS_DEVICE_UUID_OFFSET 0x00000018 // 128-bit unique device ID
#define GVBS_CURRENT_IP_ADDRESS_OFFSET 0x00000024
#define GVBS_MANUFACTURER_NAME_OFFSET 0x00000048
#define GVBS_MODEL_NAME_OFFSET 0x00000068
#define GVBS_DEVICE_VERSION_OFFSET 0x00000088
#define GVBS_SERIAL_NUMBER_OFFSET 0x000000d8
#define GVBS_USER_DEFINED_NAME_OFFSET 0x000000e8
#define GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET 0x00000200
#define GVBS_CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET 0x00000204
#define GVBS_XML_URL_0_OFFSET 0x00000220

// Additional standard GVBS registers that Aravis might check
#define GVBS_SUPPORTED_IP_CONFIG_OFFSET 0x00000020     // IP configuration options
#define GVBS_CURRENT_SUBNET_MASK_OFFSET 0x00000014     // Current subnet mask
#define GVBS_CURRENT_DEFAULT_GATEWAY_OFFSET 0x00000018 // Current gateway
#define GVBS_XML_URL_POINTER_OFFSET 0x00000064         // Pointer to XML URL string location
#define GVBS_CURRENT_IP_CONFIG_OFFSET 0x0000001C       // Current IP configuration method
#define GVBS_LINK_SPEED_OFFSET 0x0000002C              // Link speed in Mbps
#define GVBS_DEVICE_CAPABILITIES_OFFSET 0x00000010     // Device capabilities

// Standard GigE Vision control registers
#define GVBS_HEARTBEAT_TIMEOUT_OFFSET 0x00000934       // Heartbeat timeout (ms)

// Device information constants
#define DEVICE_MANUFACTURER "ESP32GenICam"
#define DEVICE_MODEL "ESP32-CAM-GigE"
#define DEVICE_VERSION "1.0.0"
#define DEVICE_SERIAL "ESP32CAM001"
#define DEVICE_USER_NAME "ESP32Camera"
#define XML_URL "Local:camera.xml;0x10000;0x2000"

// XML memory mapping
#define XML_BASE_ADDRESS 0x10000

// Bootstrap memory needs to be large enough to hold heartbeat register at 0x934 + 4 bytes
#define BOOTSTRAP_MEMORY_SIZE (0x940)

// Bootstrap register management
esp_err_t gvcp_bootstrap_init(void);
void init_bootstrap_memory(void);
uint8_t *get_bootstrap_memory(void);
size_t get_bootstrap_memory_size(void);

// Control Channel Privilege management
bool is_valid_privilege_value(uint32_t value);
uint32_t gvcp_get_control_channel_privilege(void);
void gvcp_set_control_channel_privilege(uint32_t value);
uint32_t gvcp_get_control_channel_privilege_key(void);
void gvcp_set_control_channel_privilege_key(uint32_t value);