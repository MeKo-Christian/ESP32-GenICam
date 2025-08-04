#include "streaming.h"
#include "../utils/platform.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "gvsp_streaming";

// Streaming state
static bool streaming_active = false;
static uint32_t block_id = 0;
static uint16_t packet_id = 0;
static void *client_addr = NULL;
static bool client_addr_set = false;
static uint32_t last_client_activity = 0;
static uint32_t client_timeout_ms = 30000; // 30 second timeout

// Connection health
static uint32_t connection_failures = 0;
static uint32_t max_connection_failures = 3;
static bool recovery_mode = false;
static uint32_t recovery_start_time = 0;
static uint32_t recovery_timeout_ms = 60000; // 60 second recovery timeout

// Statistics
static gvsp_streaming_stats_t stats = {0};

// Configuration
static gvsp_streaming_config_t config = {
    .sequence_tracking_enabled = true,
    .packet_timeout_ms = 1000,
    .frame_timeout_ms = 5000,
    .ring_buffer_size = 3
};

// Network send callback
static gvsp_streaming_send_callback_t send_callback = NULL;

void gvsp_streaming_set_send_callback(gvsp_streaming_send_callback_t callback) {
    send_callback = callback;
}

// Helper function to create leader packet
static gvsp_streaming_result_t create_leader_packet(uint8_t *packet, size_t *packet_size, 
                                                   const gvsp_frame_buffer_t *frame_buffer) {
    gvsp_header_t *header = (gvsp_header_t *)packet;
    gvsp_leader_data_t *leader_data = (gvsp_leader_data_t *)(packet + sizeof(gvsp_header_t));

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_LEADER;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = 0;

    // Leader data
    leader_data->flags = 0;
    leader_data->payload_type = htons(GVSP_PAYLOAD_TYPE_IMAGE);
    
    // Use platform timestamp
    uint64_t timestamp = platform->get_time_us();
    leader_data->timestamp_high = htonl((uint32_t)(timestamp >> 32));
    leader_data->timestamp_low = htonl((uint32_t)(timestamp & 0xFFFFFFFF));
    
    leader_data->pixel_format = htonl(frame_buffer->pixel_format);
    leader_data->size_x = htonl(frame_buffer->width);
    leader_data->size_y = htonl(frame_buffer->height);
    leader_data->offset_x = 0;
    leader_data->offset_y = 0;
    leader_data->padding_x = 0;
    leader_data->padding_y = 0;

    *packet_size = sizeof(gvsp_header_t) + sizeof(gvsp_leader_data_t);
    return GVSP_STREAMING_SUCCESS;
}

// Helper function to create trailer packet
static gvsp_streaming_result_t create_trailer_packet(uint8_t *packet, size_t *packet_size, 
                                                    const gvsp_frame_buffer_t *frame_buffer) {
    gvsp_header_t *header = (gvsp_header_t *)packet;
    gvsp_trailer_data_t *trailer_data = (gvsp_trailer_data_t *)(packet + sizeof(gvsp_header_t));

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_TRAILER;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = 0;

    // Trailer data
    trailer_data->reserved = 0;
    trailer_data->payload_type = htons(GVSP_PAYLOAD_TYPE_IMAGE);
    trailer_data->size_y = htonl(frame_buffer->height);

    *packet_size = sizeof(gvsp_header_t) + sizeof(gvsp_trailer_data_t);
    return GVSP_STREAMING_SUCCESS;
}

// Helper function to create data packet
static gvsp_streaming_result_t create_data_packet(uint8_t *packet, size_t *packet_size,
                                                 const uint8_t *data, size_t data_len, 
                                                 uint32_t data_offset) {
    gvsp_header_t *header = (gvsp_header_t *)packet;

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_DATA;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = htonl(data_offset);

    // Copy data
    size_t copy_len = (data_len > GVSP_DATA_PACKET_SIZE) ? GVSP_DATA_PACKET_SIZE : data_len;
    memcpy(packet + sizeof(gvsp_header_t), data, copy_len);

    *packet_size = sizeof(gvsp_header_t) + copy_len;
    return GVSP_STREAMING_SUCCESS;
}

gvsp_streaming_result_t gvsp_streaming_init(void) {
    // Initialize state
    streaming_active = false;
    block_id = 0;
    packet_id = 0;
    client_addr = NULL;
    client_addr_set = false;
    last_client_activity = 0;
    connection_failures = 0;
    recovery_mode = false;
    
    // Clear statistics
    memset(&stats, 0, sizeof(stats));
    
    platform->log_info(TAG, "GVSP streaming initialized");
    return GVSP_STREAMING_SUCCESS;
}

gvsp_streaming_result_t gvsp_streaming_start(void) {
    if (!client_addr_set) {
        platform->log_error(TAG, "Cannot start streaming: no client address set");
        return GVSP_STREAMING_ERROR;
    }

    streaming_active = true;
    last_client_activity = platform->get_time_ms();
    recovery_mode = false;
    
    platform->log_info(TAG, "GVSP streaming started");
    return GVSP_STREAMING_SUCCESS;
}

gvsp_streaming_result_t gvsp_streaming_stop(void) {
    streaming_active = false;
    platform->log_info(TAG, "GVSP streaming stopped");
    return GVSP_STREAMING_SUCCESS;
}

bool gvsp_streaming_is_active(void) {
    return streaming_active;
}

gvsp_streaming_result_t gvsp_streaming_send_frame(const gvsp_frame_buffer_t *frame_buffer) {
    if (!frame_buffer || !frame_buffer->buffer) {
        return GVSP_STREAMING_INVALID_ARG;
    }

    if (!streaming_active || !client_addr_set) {
        return GVSP_STREAMING_ERROR;
    }

    if (send_callback == NULL) {
        platform->log_error(TAG, "No send callback set");
        return GVSP_STREAMING_ERROR;
    }

    uint8_t packet[GVSP_MAX_PACKET_SIZE];
    size_t packet_size;
    gvsp_streaming_result_t result;

    // Increment block ID for new frame
    block_id++;

    // Send leader packet
    result = create_leader_packet(packet, &packet_size, frame_buffer);
    if (result != GVSP_STREAMING_SUCCESS) {
        stats.total_frame_errors++;
        return result;
    }

    if (send_callback(packet, packet_size, client_addr) != GVSP_STREAMING_SUCCESS) {
        platform->log_error(TAG, "Failed to send leader packet");
        stats.total_packet_errors++;
        stats.total_frame_errors++;
        return GVSP_STREAMING_SEND_FAILED;
    }
    stats.total_packets_sent++;

    // Send data packets
    size_t bytes_sent = 0;
    uint32_t data_offset = 0;
    
    while (bytes_sent < frame_buffer->len) {
        size_t remaining = frame_buffer->len - bytes_sent;
        size_t chunk_size = (remaining > GVSP_DATA_PACKET_SIZE) ? GVSP_DATA_PACKET_SIZE : remaining;

        result = create_data_packet(packet, &packet_size,
                                   frame_buffer->buffer + bytes_sent, chunk_size, data_offset);
        if (result != GVSP_STREAMING_SUCCESS) {
            stats.total_frame_errors++;
            return result;
        }

        if (send_callback(packet, packet_size, client_addr) != GVSP_STREAMING_SUCCESS) {
            platform->log_error(TAG, "Failed to send data packet");
            stats.total_packet_errors++;
            stats.total_frame_errors++;
            return GVSP_STREAMING_SEND_FAILED;
        }
        
        stats.total_packets_sent++;
        bytes_sent += chunk_size;
        data_offset += chunk_size;
    }

    // Send trailer packet
    result = create_trailer_packet(packet, &packet_size, frame_buffer);
    if (result != GVSP_STREAMING_SUCCESS) {
        stats.total_frame_errors++;
        return result;
    }

    if (send_callback(packet, packet_size, client_addr) != GVSP_STREAMING_SUCCESS) {
        platform->log_error(TAG, "Failed to send trailer packet");
        stats.total_packet_errors++;
        stats.total_frame_errors++;
        return GVSP_STREAMING_SEND_FAILED;
    }
    stats.total_packets_sent++;

    // Update statistics
    stats.total_frames_sent++;
    last_client_activity = platform->get_time_ms();

    platform->log_debug(TAG, "Frame sent: block_id=%d, size=%zu bytes", block_id, frame_buffer->len);
    return GVSP_STREAMING_SUCCESS;
}

gvsp_streaming_result_t gvsp_streaming_send_multipart_frame(const gvsp_frame_buffer_t *frame_buffer) {
    // For now, just send as regular frame
    // Multipart support can be added later if needed
    return gvsp_streaming_send_frame(frame_buffer);
}

gvsp_streaming_result_t gvsp_streaming_set_client_address(void *addr) {
    if (!addr) {
        return GVSP_STREAMING_INVALID_ARG;
    }

    client_addr = addr;
    client_addr_set = true;
    last_client_activity = platform->get_time_ms();
    
    platform->log_info(TAG, "Client address set for streaming");
    return GVSP_STREAMING_SUCCESS;
}

gvsp_streaming_result_t gvsp_streaming_clear_client_address(void) {
    client_addr = NULL;
    client_addr_set = false;
    streaming_active = false;
    
    platform->log_info(TAG, "Client address cleared");
    return GVSP_STREAMING_SUCCESS;
}

void gvsp_streaming_update_client_activity(void) {
    last_client_activity = platform->get_time_ms();
}

void gvsp_streaming_get_stats(gvsp_streaming_stats_t *out_stats) {
    if (out_stats) {
        *out_stats = stats;
    }
}

void gvsp_streaming_get_config(gvsp_streaming_config_t *out_config) {
    if (out_config) {
        *out_config = config;
    }
}

void gvsp_streaming_set_config(const gvsp_streaming_config_t *new_config) {
    if (new_config) {
        config = *new_config;
        platform->log_info(TAG, "Configuration updated");
    }
}

bool gvsp_streaming_is_in_recovery_mode(void) {
    return recovery_mode;
}

uint32_t gvsp_streaming_get_time_since_last_activity(void) {
    return platform->get_time_ms() - last_client_activity;
}

gvsp_streaming_result_t gvsp_streaming_reset_connection_state(void) {
    connection_failures = 0;
    recovery_mode = false;
    last_client_activity = platform->get_time_ms();
    
    platform->log_info(TAG, "Connection state reset");
    return GVSP_STREAMING_SUCCESS;
}

gvsp_streaming_result_t gvsp_streaming_validate_connection_state(void) {
    uint32_t current_time = platform->get_time_ms();
    
    // Check for client timeout
    if (client_addr_set && (current_time - last_client_activity) > client_timeout_ms) {
        platform->log_warn(TAG, "Client timeout detected");
        connection_failures++;
        
        if (connection_failures >= max_connection_failures) {
            recovery_mode = true;
            recovery_start_time = current_time;
            platform->log_warn(TAG, "Entering recovery mode");
        }
        
        return GVSP_STREAMING_ERROR;
    }
    
    // Check recovery timeout
    if (recovery_mode && (current_time - recovery_start_time) > recovery_timeout_ms) {
        platform->log_info(TAG, "Recovery timeout, clearing client");
        gvsp_streaming_clear_client_address();
        recovery_mode = false;
    }
    
    return GVSP_STREAMING_SUCCESS;
}

gvsp_streaming_result_t gvsp_streaming_resend_frame(uint32_t block_id_to_resend) {
    // For now, just log the request
    // Frame ring buffer support can be added later if needed
    platform->log_info(TAG, "Frame resend requested for block_id=%d", block_id_to_resend);
    return GVSP_STREAMING_SUCCESS;
}