#pragma once

#include "esp_err.h"
#include "camera_handler.h"
#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>

#define GVSP_PORT 50010
#define GVSP_MAX_PACKET_SIZE 1500
#define GVSP_DATA_PACKET_SIZE 1400

// GVSP Protocol Constants
#define GVSP_PACKET_TYPE_DATA        0x00
#define GVSP_PACKET_TYPE_LEADER      0x01
#define GVSP_PACKET_TYPE_TRAILER     0x02

// GVSP Status flags
#define GVSP_STATUS_SUCCESS          0x0000

// Pixel format codes
#define GVSP_PIXEL_MONO8             0x01080001

// GVSP packet header structure
typedef struct __attribute__((packed)) {
    uint8_t packet_type;
    uint8_t flags;
    uint16_t packet_id;
    uint32_t data[2];  // Format-specific data
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

esp_err_t gvsp_init(void);
esp_err_t gvsp_start_streaming(void);
esp_err_t gvsp_stop_streaming(void);
bool gvsp_is_streaming(void);
void gvsp_task(void *pvParameters);
esp_err_t gvsp_send_frame(camera_fb_t *fb);
esp_err_t gvsp_set_client_address(struct sockaddr_in *addr);
esp_err_t gvsp_clear_client_address(void);
void gvsp_update_client_activity(void);

// Statistics functions
uint32_t gvsp_get_total_packets_sent(void);
uint32_t gvsp_get_total_packet_errors(void);
uint32_t gvsp_get_total_frames_sent(void);
uint32_t gvsp_get_total_frame_errors(void);