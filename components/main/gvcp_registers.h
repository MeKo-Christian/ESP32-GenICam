#pragma once

#include "gvcp_protocol.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// --------------------------------------------------------------------------------
// Standard GVCP Bootstrap Registers (0x0000xxxx) — GigE Vision Spec
// --------------------------------------------------------------------------------

#define GVCP_TL_PARAMS_LOCKED_OFFSET 0x00000A00     // Transport Layer Parameters Locked
#define GVCP_GEVSCPS_PACKET_SIZE_OFFSET 0x00000A04  // Stream Channel Packet Size
#define GVCP_GEVSCPD_PACKET_DELAY_OFFSET 0x00000A08 // Stream Channel Packet Delay
#define GVCP_GEVSCDA_DEST_ADDRESS_OFFSET 0x00000A10 // Stream Channel Destination Address

// --------------------------------------------------------------------------------
// Stream Channel & Interface Info — Aravis Compatibility / GigE Vision 2.0+
// --------------------------------------------------------------------------------

#define GVCP_GEV_STREAM_CHANNEL_COUNT_OFFSET 0x00000D00   // GevStreamChannelCount
#define GVCP_GEV_NUM_NETWORK_INTERFACES_OFFSET 0x00000D04 // GevNumberOfNetworkInterfaces
#define GVCP_GEV_SCPHOST_PORT_OFFSET 0x00000D10           // GevSCPHostPort
#define GVCP_GEV_SCPS_PACKET_SIZE_OFFSET 0x00000D14       // GevSCPSPacketSize

#define GVCP_GEVSCCFG_REGISTER_OFFSET 0x00000D20          // GevSCCfg
#define GVCP_GEVSC_CFG_MULTIPART_OFFSET 0x00000D24        // GevSCConfigMultipart
#define GVCP_GEVSC_CFG_ARAVIS_MULTIPART_OFFSET 0x00000D30 // ArvGevSCCFGMultipartReg
#define GVCP_GEVSC_CFG_CAP_MULTIPART_OFFSET 0x00000D34    // ArvGevSCCAPMultipartReg

// --------------------------------------------------------------------------------
// GenICam Device Control Registers (0x00001xxx) — Your Custom Registers
// --------------------------------------------------------------------------------

// Acquisition Control
#define GENICAM_ACQUISITION_START_OFFSET 0x00001000
#define GENICAM_ACQUISITION_STOP_OFFSET 0x00001004
#define GENICAM_ACQUISITION_MODE_OFFSET 0x00001008

// Image Format Control
#define GENICAM_PIXEL_FORMAT_OFFSET 0x0000100C
#define GENICAM_JPEG_QUALITY_OFFSET 0x00001024
#define GENICAM_PAYLOAD_SIZE_OFFSET 0x00001020

// Stream Configuration
#define GENICAM_PACKET_DELAY_OFFSET 0x00001010
#define GENICAM_FRAME_RATE_OFFSET 0x00001014
#define GENICAM_PACKET_SIZE_OFFSET 0x00001018
#define GENICAM_STREAM_STATUS_OFFSET 0x0000101C

// Camera Parameter Control
#define GENICAM_EXPOSURE_TIME_OFFSET 0x00001030
#define GENICAM_GAIN_OFFSET 0x00001034
#define GENICAM_BRIGHTNESS_OFFSET 0x00001038
#define GENICAM_CONTRAST_OFFSET 0x0000103C
#define GENICAM_SATURATION_OFFSET 0x00001040
#define GENICAM_WHITE_BALANCE_MODE_OFFSET 0x00001044
#define GENICAM_TRIGGER_MODE_OFFSET 0x00001048

// Diagnostics and Statistics
#define GENICAM_TOTAL_COMMANDS_OFFSET 0x00001070
#define GENICAM_TOTAL_ERRORS_OFFSET 0x00001074
#define GENICAM_UNKNOWN_COMMANDS_OFFSET 0x00001078
#define GENICAM_PACKETS_SENT_OFFSET 0x0000107C
#define GENICAM_PACKET_ERRORS_OFFSET 0x00001080
#define GENICAM_FRAMES_SENT_OFFSET 0x00001084
#define GENICAM_FRAME_ERRORS_OFFSET 0x00001088
#define GENICAM_CONNECTION_STATUS_OFFSET 0x0000108C

// Frame Sequence Tracking
#define GENICAM_OUT_OF_ORDER_FRAMES_OFFSET 0x00001090
#define GENICAM_LOST_FRAMES_OFFSET 0x00001094
#define GENICAM_DUPLICATE_FRAMES_OFFSET 0x00001098
#define GENICAM_EXPECTED_SEQUENCE_OFFSET 0x0000109C
#define GENICAM_LAST_SEQUENCE_OFFSET 0x000010A0
#define GENICAM_FRAMES_IN_RING_OFFSET 0x000010A4
#define GENICAM_CONNECTION_FAILURES_OFFSET 0x000010A8
#define GENICAM_RECOVERY_MODE_OFFSET 0x000010AC

// Discovery Broadcast Control
#define GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET 0x000010B0
#define GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET 0x000010B4
#define GENICAM_DISCOVERY_BROADCASTS_SENT_OFFSET 0x000010B8
#define GENICAM_DISCOVERY_BROADCAST_FAILURES_OFFSET 0x000010BC
#define GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET 0x000010C0

// --------------------------------------------------------------------------------
// Function Declarations
// --------------------------------------------------------------------------------

// Register access command handlers
void handle_read_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr);
void handle_write_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr);
void handle_readreg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr);
void handle_writereg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr);
void handle_packetresend_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr);

// Register validation and utility functions
bool is_register_address_valid(uint32_t address);
bool is_register_address_writable(uint32_t address);
bool is_bootstrap_register(uint32_t address);
bool is_genicam_register(uint32_t address);

// Stream configuration getter functions
uint32_t gvcp_get_packet_delay_us(void);
uint32_t gvcp_get_frame_rate_fps(void);
uint32_t gvcp_get_packet_size(void);
void gvcp_set_stream_status(uint32_t status);

// Standard GVCP register management functions
uint32_t gvcp_get_tl_params_locked(void);
void gvcp_set_tl_params_locked(uint32_t locked);
uint32_t gvcp_get_stream_dest_address(void);
void gvcp_set_stream_dest_address(uint32_t dest_ip);

// Multipart payload control functions
bool gvcp_get_multipart_enabled(void);
void gvcp_set_multipart_enabled(bool enabled);
uint32_t gvcp_get_multipart_config(void);
void gvcp_set_multipart_config(uint32_t config);

// Register access initialization
esp_err_t gvcp_registers_init(void);