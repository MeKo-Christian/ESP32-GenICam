#ifndef GENICAM_REGISTERS_H
#define GENICAM_REGISTERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// --------------------------------------------------------------------------------
// Standard GVCP Bootstrap Registers (0x0000xxxx) — GigE Vision Spec
// --------------------------------------------------------------------------------

#define GVCP_TL_PARAMS_LOCKED_OFFSET 0x00000A00     // Transport Layer Parameters Locked
#define GVCP_GEVSCDA_DEST_ADDRESS_OFFSET 0x00000A10 // GevSCDA (destination IP)

// Timestamp control & value (used e.g. for action commands, time sync)
#define GVCP_GEV_TIMESTAMP_CONTROL_LATCH_OFFSET 0x00000944  // GevTimestampControlLatch
#define GVCP_GEV_TIMESTAMP_VALUE_HIGH_OFFSET 0x00000948     // GevTimestampLatchedValueHigh
#define GVCP_GEV_TIMESTAMP_VALUE_LOW_OFFSET 0x0000094C      // GevTimestampLatchedValueLow
#define GVCP_GEV_TIMESTAMP_TICK_FREQ_HIGH_OFFSET 0x0000093C // GevTimestampTickFrequencyHigh
#define GVCP_GEV_TIMESTAMP_TICK_FREQ_LOW_OFFSET 0x00000940  // GevTimestampTickFrequencyLow

// --------------------------------------------------------------------------------
// Stream Channel & Interface Info — GigE Vision 2.0+ / Aravis Compatibility
// --------------------------------------------------------------------------------

// Number of stream channels and network interfaces
#define GVCP_GEV_N_STREAM_CHANNELS_OFFSET 0x00000904    // GevStreamChannelCount
#define GVCP_GEV_N_NETWORK_INTERFACES_OFFSET 0x00000600 // GevNumberOfNetworkInterfaces

// Optional stream config extensions (Aravis-specific multipart and capability bits)
#define GVCP_GEV_SCP_HOST_PORT_OFFSET 0x00000D00
#define GVCP_GEV_SCPS_PACKET_SIZE_OFFSET 0x00000D04       // GevSCPS (stream packet size)
#define GVCP_GEV_SCPD_PACKET_DELAY_OFFSET 0x00000D08      // GevSCPD (stream packet delay)
#define GVCP_GEV_SCDA_DEST_ADDRESS_OFFSET 0x00000D18      // GevSCDA (see 0x0A10)
#define GVCP_GEVSCCFG_REGISTER_OFFSET 0x00000D20          // GevSCCfg (stream config flags)
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

// Result codes
typedef enum {
    GENICAM_REGISTERS_SUCCESS = 0,
    GENICAM_REGISTERS_ERROR = -1,
    GENICAM_REGISTERS_INVALID_ARG = -2,
    GENICAM_REGISTERS_INVALID_ADDRESS = -3,
    GENICAM_REGISTERS_WRITE_PROTECTED = -4,
    GENICAM_REGISTERS_ACCESS_DENIED = -5
} genicam_registers_result_t;

// Register access functions
genicam_registers_result_t genicam_registers_read(uint32_t address, uint32_t *value);
genicam_registers_result_t genicam_registers_write(uint32_t address, uint32_t value);
genicam_registers_result_t genicam_registers_read_memory(uint32_t address, uint8_t *buffer, size_t length);
genicam_registers_result_t genicam_registers_write_memory(uint32_t address, const uint8_t *buffer, size_t length);

// Register validation functions
bool genicam_registers_is_address_valid(uint32_t address);
bool genicam_registers_is_address_writable(uint32_t address);
bool genicam_registers_is_bootstrap_register(uint32_t address);
bool genicam_registers_is_genicam_register(uint32_t address);

// Stream configuration functions
uint32_t genicam_registers_get_packet_delay_us(void);
float genicam_registers_get_frame_rate_fps(void);
uint32_t genicam_registers_get_packet_size(void);
void genicam_registers_set_stream_status(uint32_t status);

// Standard GVCP register management functions
uint32_t genicam_registers_get_tl_params_locked(void);
void genicam_registers_set_tl_params_locked(uint32_t locked);
uint32_t genicam_registers_get_stream_dest_address(void);
void genicam_registers_set_stream_dest_address(uint32_t dest_ip);

// Multipart payload control functions
bool genicam_registers_get_multipart_enabled(void);
void genicam_registers_set_multipart_enabled(bool enabled);
uint32_t genicam_registers_get_multipart_config(void);
void genicam_registers_set_multipart_config(uint32_t config);

// Camera parameter functions
uint32_t genicam_registers_get_exposure_time(void);
void genicam_registers_set_exposure_time(uint32_t exposure_us);
uint32_t genicam_registers_get_gain(void);
void genicam_registers_set_gain(uint32_t gain);
uint32_t genicam_registers_get_pixel_format(void);
void genicam_registers_set_pixel_format(uint32_t format);

// Statistics functions
void genicam_registers_increment_total_commands(void);
void genicam_registers_increment_total_errors(void);
void genicam_registers_increment_unknown_commands(void);
uint32_t genicam_registers_get_connection_status(void);
void genicam_registers_set_connection_status_bit(uint8_t bit, bool value);

// Register access initialization
genicam_registers_result_t genicam_registers_init(void);

// Bootstrap memory callback function type (to access bootstrap registers)
typedef uint8_t* (*genicam_registers_get_bootstrap_callback_t)(void);

// Set the bootstrap memory callback (must be called before using register functions)
void genicam_registers_set_bootstrap_callback(genicam_registers_get_bootstrap_callback_t callback);

#endif // GENICAM_REGISTERS_H