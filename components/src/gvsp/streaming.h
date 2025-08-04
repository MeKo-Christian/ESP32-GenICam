#ifndef GVSP_STREAMING_H
#define GVSP_STREAMING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define GVSP_PORT 50010
#define GVSP_MAX_PACKET_SIZE 1500
#define GVSP_DATA_PACKET_SIZE 1400

// GVSP Protocol Constants
#define GVSP_PACKET_TYPE_DATA 0x00
#define GVSP_PACKET_TYPE_LEADER 0x01
#define GVSP_PACKET_TYPE_TRAILER 0x02

// GVSP Status flags
#define GVSP_STATUS_SUCCESS 0x0000

// GVSP Payload type flags for multipart support
#define GVSP_PAYLOAD_TYPE_IMAGE 0x0001
#define GVSP_PAYLOAD_TYPE_CHUNK_DATA 0x4000

// GVSP Multipart component flags
#define GVSP_COMPONENT_IMAGE 0x00
#define GVSP_COMPONENT_METADATA 0x01

// Pixel format codes (GenICam PFNC standard values)
#define GVSP_PIXEL_MONO8 0x01080001  // Mono8
#define GVSP_PIXEL_RGB565 0x02100005 // RGB565Packed
#define GVSP_PIXEL_YUV422 0x02100004 // YUV422Packed
#define GVSP_PIXEL_RGB888 0x02180014 // RGB8Packed
#define GVSP_PIXEL_JPEG 0x80000001   // JPEG

// GVSP packet header structure
typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t flags;
    uint16_t packet_id;
    uint32_t data[2]; // Format-specific data
} gvsp_header_t;

// GVSP Leader packet data
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t payload_type;
    uint32_t timestamp_high;
    uint32_t timestamp_low;
    uint32_t pixel_format;
    uint32_t size_x;
    uint32_t size_y;
    uint32_t offset_x;
    uint32_t offset_y;
    uint16_t padding_x;
    uint16_t padding_y;
} gvsp_leader_data_t;

// GVSP Trailer packet data
typedef struct __attribute__((packed)) {
    uint16_t reserved;
    uint16_t payload_type;
    uint32_t size_y;
} gvsp_trailer_data_t;

// Result codes
typedef enum {
    GVSP_STREAMING_SUCCESS = 0,
    GVSP_STREAMING_ERROR = -1,
    GVSP_STREAMING_INVALID_ARG = -2,
    GVSP_STREAMING_SEND_FAILED = -3,
    GVSP_STREAMING_NOT_INITIALIZED = -4
} gvsp_streaming_result_t;

// Frame buffer structure (platform-independent)
typedef struct {
    uint8_t *buffer;
    size_t len;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint64_t timestamp_us;
} gvsp_frame_buffer_t;

// Streaming statistics
typedef struct {
    uint32_t total_packets_sent;
    uint32_t total_packet_errors;
    uint32_t total_frames_sent;
    uint32_t total_frame_errors;
    uint32_t connection_failures;
    uint32_t frames_stored_in_ring;
    uint32_t out_of_order_frames;
    uint32_t lost_frames;
    uint32_t duplicate_frames;
    uint32_t expected_frame_sequence;
    uint32_t last_received_sequence;
} gvsp_streaming_stats_t;

// Streaming configuration
typedef struct {
    bool sequence_tracking_enabled;
    uint32_t packet_timeout_ms;
    uint32_t frame_timeout_ms;
    uint16_t ring_buffer_size;
} gvsp_streaming_config_t;

// Core streaming functions
gvsp_streaming_result_t gvsp_streaming_init(void);
gvsp_streaming_result_t gvsp_streaming_start(void);
gvsp_streaming_result_t gvsp_streaming_stop(void);
bool gvsp_streaming_is_active(void);

// Frame transmission
gvsp_streaming_result_t gvsp_streaming_send_frame(const gvsp_frame_buffer_t *frame_buffer);
gvsp_streaming_result_t gvsp_streaming_send_multipart_frame(const gvsp_frame_buffer_t *frame_buffer);

// Client address management
gvsp_streaming_result_t gvsp_streaming_set_client_address(void *client_addr);
gvsp_streaming_result_t gvsp_streaming_clear_client_address(void);
void gvsp_streaming_update_client_activity(void);

// Statistics and status
void gvsp_streaming_get_stats(gvsp_streaming_stats_t *stats);
void gvsp_streaming_get_config(gvsp_streaming_config_t *config);
void gvsp_streaming_set_config(const gvsp_streaming_config_t *config);

// Connection health
bool gvsp_streaming_is_in_recovery_mode(void);
uint32_t gvsp_streaming_get_time_since_last_activity(void);
gvsp_streaming_result_t gvsp_streaming_reset_connection_state(void);
gvsp_streaming_result_t gvsp_streaming_validate_connection_state(void);

// Frame resend support
gvsp_streaming_result_t gvsp_streaming_resend_frame(uint32_t block_id);

// Network send callback function type
typedef gvsp_streaming_result_t (*gvsp_streaming_send_callback_t)(const void *data, size_t len, void *addr);

// Set the network send callback (must be called before using streaming functions)
void gvsp_streaming_set_send_callback(gvsp_streaming_send_callback_t callback);

#endif // GVSP_STREAMING_H