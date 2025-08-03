#pragma once

#include "esp_err.h"
#include "camera_handler.h"
#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <sys/socket.h>

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
typedef struct __attribute__((packed))
{
    uint8_t packet_type;
    uint8_t flags;
    uint16_t packet_id;
    uint32_t data[2]; // Format-specific data
} gvsp_header_t;

// GVSP Leader packet data
typedef struct __attribute__((packed))
{
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
typedef struct __attribute__((packed))
{
    uint16_t reserved;
    uint16_t payload_type;
    uint32_t size_y;
} gvsp_trailer_data_t;

esp_err_t gvsp_init(void);
esp_err_t gvsp_start_streaming(void);
esp_err_t gvsp_stop_streaming(void);
bool gvsp_is_streaming(void);
void gvsp_task(void *pvParameters);
esp_err_t gvsp_send_frame(local_camera_fb_t *fb);
esp_err_t gvsp_set_client_address(struct sockaddr_in *addr);
esp_err_t gvsp_clear_client_address(void);
void gvsp_update_client_activity(void);

// Statistics functions
uint32_t gvsp_get_total_packets_sent(void);
uint32_t gvsp_get_total_packet_errors(void);
uint32_t gvsp_get_total_frames_sent(void);
uint32_t gvsp_get_total_frame_errors(void);

// Recovery and connection health functions
uint32_t gvsp_get_connection_failures(void);
bool gvsp_is_in_recovery_mode(void);
uint32_t gvsp_get_time_since_last_activity(void);
esp_err_t gvsp_reset_connection_state(void);
esp_err_t gvsp_validate_connection_state(void);
esp_err_t gvsp_force_cleanup(void);

// Frame ring buffer functions
uint32_t gvsp_get_frames_stored_in_ring(void);
esp_err_t gvsp_resend_frame(uint32_t block_id);

// Multipart payload support
esp_err_t gvsp_send_multipart_frame(local_camera_fb_t *fb);
esp_err_t gvsp_send_component(local_camera_fb_t *fb, uint8_t component_type, uint16_t component_index);

// Frame sequence tracking functions
uint32_t gvsp_get_out_of_order_frames(void);
uint32_t gvsp_get_lost_frames(void);
uint32_t gvsp_get_duplicate_frames(void);
uint32_t gvsp_get_expected_frame_sequence(void);
uint32_t gvsp_get_last_received_sequence(void);
esp_err_t gvsp_set_sequence_tracking(bool enabled);
bool gvsp_is_sequence_tracking_enabled(void);