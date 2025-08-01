#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>

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

// GVCP Error Status Codes (for NACK responses)
#define GVCP_ERROR_NOT_IMPLEMENTED   0x8001
#define GVCP_ERROR_INVALID_PARAMETER 0x8002
#define GVCP_ERROR_INVALID_ADDRESS   0x8003
#define GVCP_ERROR_WRITE_PROTECT     0x8004
#define GVCP_ERROR_BAD_ALIGNMENT     0x8005
#define GVCP_ERROR_ACCESS_DENIED     0x8006
#define GVCP_ERROR_BUSY              0x8007
#define GVCP_ERROR_MSG_TIMEOUT       0x800B
#define GVCP_ERROR_INVALID_HEADER    0x800E
#define GVCP_ERROR_WRONG_CONFIG      0x800F

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

// Image format control registers
#define GENICAM_PIXEL_FORMAT_OFFSET         0x0000100C
#define GENICAM_JPEG_QUALITY_OFFSET         0x00001024

// Camera control registers (matching GenICam XML)
#define GENICAM_EXPOSURE_TIME_OFFSET        0x00001030
#define GENICAM_GAIN_OFFSET                 0x00001034
#define GENICAM_BRIGHTNESS_OFFSET           0x00001038
#define GENICAM_CONTRAST_OFFSET             0x0000103C
#define GENICAM_SATURATION_OFFSET           0x00001040
#define GENICAM_WHITE_BALANCE_MODE_OFFSET   0x00001044
#define GENICAM_TRIGGER_MODE_OFFSET         0x00001048

// Stream control registers
#define GENICAM_PACKET_DELAY_OFFSET         0x00001010
#define GENICAM_FRAME_RATE_OFFSET           0x00001014
#define GENICAM_PACKET_SIZE_OFFSET          0x00001018
#define GENICAM_STREAM_STATUS_OFFSET        0x0000101C
#define GENICAM_PAYLOAD_SIZE_OFFSET         0x00001020

// Diagnostic and statistics registers (moved to avoid collision)
#define GENICAM_TOTAL_COMMANDS_OFFSET       0x00001070
#define GENICAM_TOTAL_ERRORS_OFFSET         0x00001074
#define GENICAM_UNKNOWN_COMMANDS_OFFSET     0x00001078
#define GENICAM_PACKETS_SENT_OFFSET         0x0000107C
#define GENICAM_PACKET_ERRORS_OFFSET        0x00001080
#define GENICAM_FRAMES_SENT_OFFSET          0x00001084
#define GENICAM_FRAME_ERRORS_OFFSET         0x00001088
#define GENICAM_CONNECTION_STATUS_OFFSET    0x0000108C

// Frame sequence tracking registers
#define GENICAM_OUT_OF_ORDER_FRAMES_OFFSET  0x00001090
#define GENICAM_LOST_FRAMES_OFFSET          0x00001094
#define GENICAM_DUPLICATE_FRAMES_OFFSET     0x00001098
#define GENICAM_EXPECTED_SEQUENCE_OFFSET    0x0000109C
#define GENICAM_LAST_SEQUENCE_OFFSET        0x000010A0
#define GENICAM_FRAMES_IN_RING_OFFSET       0x000010A4
#define GENICAM_CONNECTION_FAILURES_OFFSET  0x000010A8
#define GENICAM_RECOVERY_MODE_OFFSET        0x000010AC

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

// Error handling functions
esp_err_t gvcp_send_nack(const gvcp_header_t *original_header, uint16_t error_code, struct sockaddr_in *client_addr);

// Stream configuration getter functions
uint32_t gvcp_get_packet_delay_us(void);
uint32_t gvcp_get_frame_rate_fps(void);
uint32_t gvcp_get_packet_size(void);
void gvcp_set_stream_status(uint32_t status);

// Error statistics functions
uint32_t gvcp_get_total_commands_received(void);
uint32_t gvcp_get_total_errors_sent(void);
uint32_t gvcp_get_total_unknown_commands(void);

// Connection status management
void gvcp_set_connection_status_bit(uint8_t bit_position, bool value);
uint32_t gvcp_get_connection_status(void);