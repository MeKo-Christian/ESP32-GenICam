#pragma once

#include "esp_err.h"
#include <stdint.h>

#define GVCP_PORT 3956

// GVCP Protocol Constants
#define GVCP_PACKET_TYPE_ACK         0x00
#define GVCP_PACKET_TYPE_CMD         0x42
#define GVCP_PACKET_TYPE_ERROR       0x80

#define GVCP_CMD_DISCOVERY           0x0002
#define GVCP_ACK_DISCOVERY           0x0003
#define GVCP_CMD_READ_MEMORY         0x0084
#define GVCP_ACK_READ_MEMORY         0x0085
#define GVCP_CMD_WRITE_MEMORY        0x0086
#define GVCP_ACK_WRITE_MEMORY        0x0087

#define GVCP_FLAGS_ACK_REQUIRED      0x01

#define GVBS_DISCOVERY_DATA_SIZE     0xf8

// Bootstrap register offsets (from Aravis GVBS definitions)
#define GVBS_VERSION_OFFSET                  0x00000000
#define GVBS_DEVICE_MODE_OFFSET             0x00000004
#define GVBS_DEVICE_MAC_ADDRESS_HIGH_OFFSET 0x00000008
#define GVBS_DEVICE_MAC_ADDRESS_LOW_OFFSET  0x0000000c
#define GVBS_CURRENT_IP_ADDRESS_OFFSET      0x00000024
#define GVBS_MANUFACTURER_NAME_OFFSET       0x00000048
#define GVBS_MODEL_NAME_OFFSET              0x00000068
#define GVBS_DEVICE_VERSION_OFFSET          0x00000088
#define GVBS_SERIAL_NUMBER_OFFSET           0x000000d8
#define GVBS_USER_DEFINED_NAME_OFFSET       0x000000e8
#define GVBS_XML_URL_0_OFFSET               0x00000200

// Acquisition control registers (custom addresses beyond bootstrap region)
#define GENICAM_ACQUISITION_START_OFFSET    0x00001000
#define GENICAM_ACQUISITION_STOP_OFFSET     0x00001004
#define GENICAM_ACQUISITION_MODE_OFFSET     0x00001008

// Stream control registers
#define GENICAM_PACKET_DELAY_OFFSET         0x00001010
#define GENICAM_FRAME_RATE_OFFSET           0x00001014
#define GENICAM_PACKET_SIZE_OFFSET          0x00001018
#define GENICAM_STREAM_STATUS_OFFSET        0x0000101C

// GVCP packet header structure
typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t packet_flags;
    uint16_t command;
    uint16_t size;
    uint16_t id;
} gvcp_header_t;

esp_err_t gvcp_init(void);
void gvcp_task(void *pvParameters);

// Stream configuration getter functions
uint32_t gvcp_get_packet_delay_us(void);
uint32_t gvcp_get_frame_rate_fps(void);
uint32_t gvcp_get_packet_size(void);
void gvcp_set_stream_status(uint32_t status);