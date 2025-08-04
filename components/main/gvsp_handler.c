#include "gvsp_handler.h"
#include "camera_handler.h"
#include "gvcp_handler.h"
#include "gvcp_registers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
// Include sys/socket.h for struct sockaddr_in in header
#include <sys/socket.h>

static const char *TAG = "gvsp_handler";

static int gvsp_sock = -1;

// Forward declarations
static esp_err_t gvsp_handle_connection_failure(void);
static esp_err_t gvsp_check_recovery_timeout(void);
static bool streaming_active = false;
static uint32_t block_id = 0;
static uint16_t packet_id = 0;
static SemaphoreHandle_t streaming_mutex = NULL;
static struct sockaddr_in client_addr;
static bool client_addr_set = false;
static uint32_t last_client_activity = 0;
static uint32_t client_timeout_ms = 30000; // 30 second timeout

// Enhanced timeout and recovery management
static uint32_t last_heartbeat_check = 0;
static uint32_t heartbeat_interval_ms = 5000; // Check every 5 seconds
static uint32_t connection_failures = 0;
static uint32_t max_connection_failures = 3;
static bool recovery_mode = false;
static uint32_t recovery_start_time = 0;
static uint32_t recovery_timeout_ms = 60000; // 60 second recovery timeout

// Socket health monitoring
static uint32_t socket_error_count = 0;
static uint32_t max_socket_errors = 5;
static uint32_t last_socket_recreation = 0;
static uint32_t socket_recreation_interval_ms = 10000; // Min 10s between recreations

// Frame buffer ring for resend capability
#define FRAME_RING_BUFFER_SIZE 3
typedef struct
{
    uint8_t *data;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint32_t block_id;
    uint32_t sequence_number;
    uint32_t timestamp;
    bool valid;
} frame_ring_entry_t;

static frame_ring_entry_t frame_ring[FRAME_RING_BUFFER_SIZE];
static uint8_t frame_ring_head = 0;
static uint32_t frames_stored = 0;
static SemaphoreHandle_t frame_ring_mutex = NULL;

// Frame sequence tracking and validation
static uint32_t expected_frame_sequence = 1;
static uint32_t last_received_sequence = 0;
static uint32_t out_of_order_frames = 0;
static uint32_t lost_frames = 0;
static uint32_t duplicate_frames = 0;
static bool sequence_tracking_enabled = true;

// Error handling and statistics
static uint32_t total_packets_sent = 0;
static uint32_t total_packet_errors = 0;
static uint32_t total_frames_sent = 0;
static uint32_t total_frame_errors = 0;

// Forward declarations for multipart support functions
static esp_err_t gvsp_send_leader_packet_multipart(uint32_t frame_size, uint32_t width, uint32_t height,
                                                   int pixel_format, uint16_t payload_type, uint16_t component_index);
static esp_err_t gvsp_send_trailer_packet_multipart(uint32_t height, uint16_t payload_type, uint16_t component_index);

esp_err_t gvsp_init(void)
{
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(GVSP_PORT);

    gvsp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (gvsp_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create GVSP socket: errno %d", errno);
        return ESP_FAIL;
    }
    PROTOCOL_LOG_I(TAG, "GVSP socket created");

    int err = bind(gvsp_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "GVSP socket unable to bind: errno %d", errno);
        close(gvsp_sock);
        gvsp_sock = -1;
        return ESP_FAIL;
    }
    PROTOCOL_LOG_I(TAG, "GVSP socket bound to port %d", GVSP_PORT);

    // Configure socket send buffer for ESP32 memory constraints
    int send_buffer_size = 8192; // 8KB send buffer for ESP32
    if (setsockopt(gvsp_sock, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set socket send buffer size: errno %d", errno);
    }
    else
    {
        PROTOCOL_LOG_I(TAG, "GVSP socket send buffer configured to %d bytes", send_buffer_size);
    }

    // Configure socket receive buffer (for potential future use)
    int recv_buffer_size = 4096; // 4KB receive buffer
    if (setsockopt(gvsp_sock, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set socket receive buffer size: errno %d", errno);
    }
    else
    {
        PROTOCOL_LOG_I(TAG, "GVSP socket receive buffer configured to %d bytes", recv_buffer_size);
    }

    // Set GVSP socket active bit in connection status
    gvcp_set_connection_status_bit(1, true);

    // Create mutex for streaming control
    streaming_mutex = xSemaphoreCreateMutex();
    if (streaming_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create streaming mutex");
        close(gvsp_sock);
        gvsp_sock = -1;
        return ESP_FAIL;
    }

    // Create mutex for frame ring buffer
    frame_ring_mutex = xSemaphoreCreateMutex();
    if (frame_ring_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create frame ring mutex");
        close(gvsp_sock);
        gvsp_sock = -1;
        return ESP_FAIL;
    }

    // Initialize frame ring buffer
    memset(frame_ring, 0, sizeof(frame_ring));
    frame_ring_head = 0;
    frames_stored = 0;

    ESP_LOGI(TAG, "Frame ring buffer initialized with %d slots", FRAME_RING_BUFFER_SIZE);

    return ESP_OK;
}

// Socket recreation for network failure recovery
static esp_err_t gvsp_recreate_socket(void)
{
    uint32_t current_time = esp_log_timestamp();

    // Rate limiting: don't recreate socket too frequently
    if (current_time - last_socket_recreation < socket_recreation_interval_ms)
    {
        ESP_LOGW(TAG, "Socket recreation rate limited, skipping");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Recreating GVSP socket due to network errors");

    // Close existing socket
    if (gvsp_sock >= 0)
    {
        close(gvsp_sock);
        gvsp_sock = -1;
        gvcp_set_connection_status_bit(1, false); // Clear GVSP socket active bit
    }

    // Clear client connection state since socket is being recreated
    if (client_addr_set)
    {
        gvsp_clear_client_address();
    }

    // Recreate socket
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(GVSP_PORT);

    gvsp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (gvsp_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to recreate GVSP socket: errno %d", errno);
        return ESP_FAIL;
    }

    int err = bind(gvsp_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0)
    {
        ESP_LOGE(TAG, "GVSP socket unable to bind after recreation: errno %d", errno);
        close(gvsp_sock);
        gvsp_sock = -1;
        return ESP_FAIL;
    }

    // Reconfigure socket buffers after recreation
    int send_buffer_size = 8192; // 8KB send buffer for ESP32
    if (setsockopt(gvsp_sock, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set socket send buffer size after recreation: errno %d", errno);
    }

    int recv_buffer_size = 4096; // 4KB receive buffer
    if (setsockopt(gvsp_sock, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0)
    {
        ESP_LOGW(TAG, "Failed to set socket receive buffer size after recreation: errno %d", errno);
    }

    // Reset socket error count and update status
    socket_error_count = 0;
    last_socket_recreation = current_time;
    gvcp_set_connection_status_bit(1, true); // Set GVSP socket active bit

    PROTOCOL_LOG_I(TAG, "GVSP socket successfully recreated and bound to port %d", GVSP_PORT);
    return ESP_OK;
}

// Frame ring buffer management
static esp_err_t gvsp_store_frame_in_ring(local_camera_fb_t *fb, uint32_t block_id_used)
{
    if (frame_ring_mutex == NULL || fb == NULL || fb->buf == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(frame_ring_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to take frame ring mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Get current slot
    frame_ring_entry_t *slot = &frame_ring[frame_ring_head];

    // Free existing data if present
    if (slot->data != NULL)
    {
        free(slot->data);
        slot->data = NULL;
    }

    // Allocate and copy frame data
    slot->data = malloc(fb->len);
    if (slot->data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for frame ring entry");
        xSemaphoreGive(frame_ring_mutex);
        return ESP_ERR_NO_MEM;
    }

    memcpy(slot->data, fb->buf, fb->len);
    slot->size = fb->len;
    slot->width = fb->width;
    slot->height = fb->height;
    slot->block_id = block_id_used;
    slot->sequence_number = block_id_used;                     // Use block_id as sequence number
    slot->timestamp = (uint32_t)(esp_timer_get_time() / 1000); // Convert to milliseconds for storage
    slot->valid = true;

    // Move to next slot
    frame_ring_head = (frame_ring_head + 1) % FRAME_RING_BUFFER_SIZE;
    if (frames_stored < FRAME_RING_BUFFER_SIZE)
    {
        frames_stored++;
    }

    xSemaphoreGive(frame_ring_mutex);

    ESP_LOGD(TAG, "Stored frame with block_id %d in ring buffer (slot %d, total stored: %d)",
             block_id_used, (frame_ring_head == 0) ? FRAME_RING_BUFFER_SIZE - 1 : frame_ring_head - 1,
             frames_stored);

    return ESP_OK;
}

static esp_err_t gvsp_get_frame_from_ring(uint32_t block_id, local_camera_fb_t *fb_out)
{
    if (frame_ring_mutex == NULL || fb_out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(frame_ring_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Failed to take frame ring mutex for retrieval");
        return ESP_ERR_TIMEOUT;
    }

    // Search for the requested block_id
    for (int i = 0; i < FRAME_RING_BUFFER_SIZE; i++)
    {
        if (frame_ring[i].valid && frame_ring[i].block_id == block_id)
        {
            // Found the frame, copy data
            fb_out->buf = frame_ring[i].data;
            fb_out->len = frame_ring[i].size;
            fb_out->width = frame_ring[i].width;
            fb_out->height = frame_ring[i].height;

            xSemaphoreGive(frame_ring_mutex);
            PROTOCOL_LOG_I(TAG, "Retrieved frame with block_id %d from ring buffer", block_id);
            return ESP_OK;
        }
    }

    xSemaphoreGive(frame_ring_mutex);
    ESP_LOGW(TAG, "Frame with block_id %d not found in ring buffer", block_id);
    return ESP_ERR_NOT_FOUND;
}

static void gvsp_clear_frame_ring(void)
{
    if (frame_ring_mutex == NULL)
    {
        return;
    }

    if (xSemaphoreTake(frame_ring_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        for (int i = 0; i < FRAME_RING_BUFFER_SIZE; i++)
        {
            if (frame_ring[i].data != NULL)
            {
                free(frame_ring[i].data);
                frame_ring[i].data = NULL;
            }
            frame_ring[i].valid = false;
        }
        frame_ring_head = 0;
        frames_stored = 0;
        xSemaphoreGive(frame_ring_mutex);
        PROTOCOL_LOG_I(TAG, "Frame ring buffer cleared");
    }
    else
    {
        ESP_LOGW(TAG, "Failed to clear frame ring buffer - mutex timeout");
    }
}

// Frame sequence tracking and validation
static esp_err_t gvsp_validate_frame_sequence(uint32_t received_sequence)
{
    if (!sequence_tracking_enabled)
    {
        return ESP_OK;
    }

    // First frame or after reset
    if (expected_frame_sequence == 1 && last_received_sequence == 0)
    {
        expected_frame_sequence = received_sequence + 1;
        last_received_sequence = received_sequence;
        PROTOCOL_LOG_I(TAG, "Frame sequence tracking started at sequence %d", received_sequence);
        return ESP_OK;
    }

    // Check for expected sequence
    if (received_sequence == expected_frame_sequence)
    {
        // Perfect sequence
        expected_frame_sequence++;
        last_received_sequence = received_sequence;
        return ESP_OK;
    }

    // Check for duplicate frame
    if (received_sequence <= last_received_sequence)
    {
        duplicate_frames++;
        ESP_LOGW(TAG, "Duplicate frame detected: received=%d, last=%d (total duplicates: %d)",
                 received_sequence, last_received_sequence, duplicate_frames);
        return ESP_ERR_INVALID_STATE;
    }

    // Check for out-of-order or lost frames
    if (received_sequence > expected_frame_sequence)
    {
        uint32_t gap = received_sequence - expected_frame_sequence;
        lost_frames += gap;
        ESP_LOGW(TAG, "Frame sequence gap detected: expected=%d, received=%d, lost=%d frames (total lost: %d)",
                 expected_frame_sequence, received_sequence, gap, lost_frames);

        // Update tracking
        expected_frame_sequence = received_sequence + 1;
        last_received_sequence = received_sequence;
        return ESP_ERR_NOT_FOUND; // Indicates lost frames
    }

    // Out of order (received < expected but > last_received)
    out_of_order_frames++;
    ESP_LOGW(TAG, "Out-of-order frame: expected=%d, received=%d (total out-of-order: %d)",
             expected_frame_sequence, received_sequence, out_of_order_frames);

    last_received_sequence = received_sequence;
    return ESP_ERR_INVALID_RESPONSE; // Indicates out-of-order
}

static void gvsp_reset_sequence_tracking(void)
{
    expected_frame_sequence = 1;
    last_received_sequence = 0;
    out_of_order_frames = 0;
    lost_frames = 0;
    duplicate_frames = 0;
    PROTOCOL_LOG_I(TAG, "Frame sequence tracking reset");
}

esp_err_t gvsp_start_streaming(void)
{
    if (xSemaphoreTake(streaming_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    streaming_active = true;
    block_id = 1; // Start with block ID 1
    packet_id = 0;

    // Reset sequence tracking when starting streaming
    gvsp_reset_sequence_tracking();

    ESP_LOGI(TAG, "GVSP streaming started");

    xSemaphoreGive(streaming_mutex);
    return ESP_OK;
}

esp_err_t gvsp_stop_streaming(void)
{
    if (xSemaphoreTake(streaming_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    streaming_active = false;

    // Clean up streaming state
    block_id = 0;
    packet_id = 0;

    // Clear frame ring buffer
    gvsp_clear_frame_ring();

    // Update stream status
    gvcp_set_stream_status(0x0000); // Clear all status bits

    PROTOCOL_LOG_I(TAG, "GVSP streaming stopped and cleaned up");

    xSemaphoreGive(streaming_mutex);
    return ESP_OK;
}

bool gvsp_is_streaming(void)
{
    bool is_streaming = false;

    if (xSemaphoreTake(streaming_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        is_streaming = streaming_active;
        xSemaphoreGive(streaming_mutex);
    }

    return is_streaming;
}

// Helper function to send UDP packet with retry and socket error detection
static esp_err_t gvsp_send_udp_packet(const uint8_t *packet, size_t packet_size, int max_retries)
{
    // Check if socket is valid
    if (gvsp_sock < 0)
    {
        ESP_LOGE(TAG, "Invalid socket for packet transmission");
        socket_error_count++;
        return ESP_FAIL;
    }

    // Log detailed client address information on first packet or errors
    if (total_packets_sent == 0 || socket_error_count > 0)
    {
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), addr_str, INET_ADDRSTRLEN);
        PROTOCOL_LOG_I(TAG, "Sending UDP packet: size=%zu, dest=%s:%d, socket=%d, retry_count=%d",
                       packet_size, addr_str, ntohs(client_addr.sin_port), gvsp_sock, socket_error_count);
    }

    for (int retry = 0; retry < max_retries; retry++)
    {
        int err = sendto(gvsp_sock, packet, packet_size, 0,
                         (struct sockaddr *)&client_addr, sizeof(client_addr));
        if (err >= 0)
        {
            total_packets_sent++;
            // Reset socket error count on successful send
            if (socket_error_count > 0)
            {
                ESP_LOGI(TAG, "Socket recovered after %d errors", socket_error_count);
                socket_error_count = 0;
            }
            PROTOCOL_LOG_D(TAG, "UDP packet sent successfully: %zu bytes", packet_size);
            return ESP_OK;
        }
        else
        {
            total_packet_errors++;
            socket_error_count++;

            // Enhanced error logging with network context
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(client_addr.sin_addr), addr_str, INET_ADDRSTRLEN);
            ESP_LOGW(TAG, "Send failed (attempt %d/%d): errno %d (%s), dest=%s:%d, size=%zu",
                     retry + 1, max_retries, errno, strerror(errno),
                     addr_str, ntohs(client_addr.sin_port), packet_size);

            // Check for specific network errors that indicate socket issues
            if (errno == EBADF || errno == ENOTSOCK || errno == ENETDOWN || errno == ENETUNREACH)
            {
                ESP_LOGW(TAG, "Network/socket error detected: errno %d (%s)", errno, strerror(errno));

                // Trigger socket recreation if we've had enough errors
                if (socket_error_count >= max_socket_errors)
                {
                    ESP_LOGW(TAG, "Max socket errors reached (%d), attempting socket recreation",
                             socket_error_count);
                    esp_err_t recreate_result = gvsp_recreate_socket();
                    if (recreate_result == ESP_OK)
                    {
                        // Retry once with new socket
                        err = sendto(gvsp_sock, packet, packet_size, 0,
                                     (struct sockaddr *)&client_addr, sizeof(client_addr));
                        if (err >= 0)
                        {
                            total_packets_sent++;
                            return ESP_OK;
                        }
                    }
                }

                // Break out of retry loop for socket errors
                break;
            }

            // Handle ENOMEM (errno 12) - insufficient buffer space
            else if (errno == ENOMEM || errno == ENOBUFS)
            {
                ESP_LOGW(TAG, "Buffer exhaustion detected: errno %d (%s), packet_size=%zu",
                         errno, strerror(errno), packet_size);

                // For buffer exhaustion, add a small delay to allow buffers to drain
                if (retry < max_retries - 1)
                {
                    vTaskDelay(pdMS_TO_TICKS(10 + retry * 5)); // Progressive backoff: 10ms, 15ms, 20ms
                    PROTOCOL_LOG_I(TAG, "Buffer recovery delay completed, retrying packet transmission");
                }
            }

            if (retry < max_retries - 1)
            {
                vTaskDelay(pdMS_TO_TICKS(1)); // Small delay before retry
            }
        }
    }

    ESP_LOGE(TAG, "Failed to send packet after %d attempts", max_retries);
    return ESP_FAIL;
}

// Helper function to convert camera pixel format to GVSP pixel format
static uint32_t camera_format_to_gvsp_format(int camera_format)
{
    switch (camera_format)
    {
    case CAMERA_PIXFORMAT_MONO8:
        return GVSP_PIXEL_MONO8;
    case CAMERA_PIXFORMAT_RGB565:
        return GVSP_PIXEL_RGB565;
    case CAMERA_PIXFORMAT_YUV422:
        return GVSP_PIXEL_YUV422;
    case CAMERA_PIXFORMAT_RGB888:
        return GVSP_PIXEL_RGB888;
    case CAMERA_PIXFORMAT_JPEG:
        return GVSP_PIXEL_JPEG;
    default:
        return GVSP_PIXEL_MONO8; // Default fallback
    }
}

static esp_err_t gvsp_send_leader_packet(uint32_t frame_size, uint32_t width, uint32_t height, int pixel_format)
{
    uint8_t packet[sizeof(gvsp_header_t) + sizeof(gvsp_leader_data_t)];
    gvsp_header_t *header = (gvsp_header_t *)packet;
    gvsp_leader_data_t *leader_data = (gvsp_leader_data_t *)(packet + sizeof(gvsp_header_t));

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_LEADER;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = 0; // Reserved

    // Leader data
    leader_data->flags = htons(0);
    leader_data->payload_type = htons(0x0001); // Image payload type

    // Use high-precision timer for accurate timestamps
    uint64_t timestamp_us = esp_timer_get_time(); // Microseconds since boot
    leader_data->timestamp_high = htonl((uint32_t)(timestamp_us >> 32));
    leader_data->timestamp_low = htonl((uint32_t)(timestamp_us & 0xFFFFFFFF));
    leader_data->pixel_format = htonl(camera_format_to_gvsp_format(pixel_format));
    leader_data->size_x = htonl(width);
    leader_data->size_y = htonl(height);
    leader_data->offset_x = 0;
    leader_data->offset_y = 0;
    leader_data->padding_x = 0;
    leader_data->padding_y = 0;

    if (!client_addr_set)
    {
        ESP_LOGW(TAG, "No client address set for streaming");
        return ESP_ERR_INVALID_STATE;
    }

    // Validate client address configuration
    if (client_addr.sin_addr.s_addr == 0 || client_addr.sin_port == 0)
    {
        char addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), addr_str, INET_ADDRSTRLEN);
        ESP_LOGW(TAG, "Invalid client address configuration for leader packet: %s:%d", addr_str, ntohs(client_addr.sin_port));
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(packet), 3);
    if (err == ESP_OK)
    {
        PROTOCOL_LOG_D(TAG, "Sent GVSP leader packet (%d bytes)", sizeof(packet));
    }

    return err;
}

static esp_err_t gvsp_send_data_packet(const uint8_t *data, uint32_t data_size)
{
    uint8_t packet[sizeof(gvsp_header_t) + data_size];
    gvsp_header_t *header = (gvsp_header_t *)packet;

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_DATA;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = 0; // Packet data - could be used for data offset

    // Copy image data
    memcpy(packet + sizeof(gvsp_header_t), data, data_size);

    if (!client_addr_set)
    {
        ESP_LOGW(TAG, "No client address set for streaming");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(gvsp_header_t) + data_size, 2);
    if (err == ESP_OK)
    {
        PROTOCOL_LOG_D(TAG, "Sent GVSP data packet (%d bytes)", sizeof(gvsp_header_t) + data_size);
    }

    return err;
}

static esp_err_t gvsp_send_trailer_packet(uint32_t height)
{
    uint8_t packet[sizeof(gvsp_header_t) + sizeof(gvsp_trailer_data_t)];
    gvsp_header_t *header = (gvsp_header_t *)packet;
    gvsp_trailer_data_t *trailer_data = (gvsp_trailer_data_t *)(packet + sizeof(gvsp_header_t));

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_TRAILER;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = 0; // Reserved

    // Trailer data
    trailer_data->reserved = 0;
    trailer_data->payload_type = htons(0x0001); // Image payload type
    trailer_data->size_y = htonl(height);

    if (!client_addr_set)
    {
        ESP_LOGW(TAG, "No client address set for streaming");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(packet), 3);
    if (err == ESP_OK)
    {
        PROTOCOL_LOG_D(TAG, "Sent GVSP trailer packet (%d bytes)", sizeof(packet));
    }

    return err;
}

esp_err_t gvsp_send_frame(local_camera_fb_t *fb)
{
    if (!streaming_active || !client_addr_set)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (fb == NULL || fb->buf == NULL || fb->len == 0)
    {
        ESP_LOGE(TAG, "Invalid frame buffer");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if multipart mode is enabled
    if (gvcp_get_multipart_enabled())
    {
        ESP_LOGI(TAG, "Sending frame in multipart mode");
        return gvsp_send_multipart_frame(fb);
    }

    // Log frame transmission details with client information
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), addr_str, INET_ADDRSTRLEN);
    PROTOCOL_LOG_I(TAG, "Sending frame: block_id=%d, size=%d, %dx%d, dest=%s:%d, packets_sent=%d",
                   block_id, fb->len, fb->width, fb->height,
                   addr_str, ntohs(client_addr.sin_port), total_packets_sent);

    // Validate frame sequence (use block_id as sequence number)
    esp_err_t seq_err = gvsp_validate_frame_sequence(block_id);
    if (seq_err != ESP_OK)
    {
        // Log sequence issues but continue transmission
        switch (seq_err)
        {
        case ESP_ERR_INVALID_STATE:
            ESP_LOGW(TAG, "Duplicate frame sequence detected for block_id %d", block_id);
            break;
        case ESP_ERR_NOT_FOUND:
            ESP_LOGW(TAG, "Lost frame(s) detected before block_id %d", block_id);
            break;
        case ESP_ERR_INVALID_RESPONSE:
            ESP_LOGW(TAG, "Out-of-order frame detected for block_id %d", block_id);
            break;
        default:
            break;
        }
    }

    // Store frame in ring buffer before sending (for potential resend)
    esp_err_t store_err = gvsp_store_frame_in_ring(fb, block_id);
    if (store_err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to store frame in ring buffer: %s", esp_err_to_name(store_err));
        // Continue with transmission even if storage fails
    }

    // Send leader packet
    esp_err_t err = gvsp_send_leader_packet(fb->len, fb->width, fb->height, fb->format);
    if (err != ESP_OK)
    {
        return err;
    }

    // Send data packets
    uint32_t bytes_sent = 0;
    const uint8_t *data_ptr = fb->buf;
    uint32_t configured_packet_size = gvcp_get_packet_size();
    uint32_t packet_delay_us = gvcp_get_packet_delay_us();

    while (bytes_sent < fb->len)
    {
        uint32_t chunk_size = fb->len - bytes_sent;
        if (chunk_size > configured_packet_size)
        {
            chunk_size = configured_packet_size;
        }

        err = gvsp_send_data_packet(data_ptr + bytes_sent, chunk_size);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send data packet at offset %d", bytes_sent);
            gvcp_set_stream_status(0x8000); // Set error bit
            return err;
        }

        bytes_sent += chunk_size;

        // Configurable delay between packets
        if (packet_delay_us > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(packet_delay_us / 1000));
        }
    }

    // Send trailer packet
    err = gvsp_send_trailer_packet(fb->height);
    if (err != ESP_OK)
    {
        total_frame_errors++;
        gvcp_set_stream_status(0x8000); // Set error bit
        return err;
    }

    // Increment block ID for next frame
    block_id++;
    total_frames_sent++;

    uint32_t total_packets = (fb->len + configured_packet_size - 1) / configured_packet_size + 2;
    PROTOCOL_LOG_I(TAG, "Frame sent successfully: %d packets, block_id=%d", total_packets, block_id);

    // Update stream status with success
    gvcp_set_stream_status(0x0001); // Active streaming bit

    return ESP_OK;
}

void gvsp_task(void *pvParameters)
{
    PROTOCOL_LOG_I(TAG, "GVSP task started");
    last_heartbeat_check = esp_log_timestamp();

    // Add this task to watchdog monitoring
    esp_task_wdt_add(NULL);

    while (1)
    {
        // Feed the watchdog
        esp_task_wdt_reset();
        uint32_t current_time = esp_log_timestamp();

        // Enhanced heartbeat and recovery management
        if (current_time - last_heartbeat_check >= heartbeat_interval_ms)
        {
            last_heartbeat_check = current_time;

            // Check recovery timeout
            gvsp_check_recovery_timeout();

            // Validate connection state consistency
            gvsp_validate_connection_state();

            // Check for client timeout (only if not in recovery mode)
            if (client_addr_set && !recovery_mode)
            {
                if (current_time - last_client_activity > client_timeout_ms)
                {
                    ESP_LOGW(TAG, "Client timeout detected (last activity: %d ms ago)",
                             current_time - last_client_activity);

                    esp_err_t err = gvsp_handle_connection_failure();
                    if (err == ESP_ERR_TIMEOUT)
                    {
                        // In recovery mode now, set timeout status
                        gvcp_set_stream_status(0x4000); // Set timeout bit
                    }
                    else
                    {
                        // Just a single timeout, stop streaming but keep connection
                        gvsp_stop_streaming();
                        gvcp_set_stream_status(0x2000); // Set warning bit
                    }
                }
            }
        }

        // Check if streaming is active
        if (gvsp_is_streaming() && client_addr_set)
        {
            // Capture a frame
            local_camera_fb_t *fb = NULL;
            esp_err_t ret = camera_capture_frame(&fb);

            if (ret == ESP_OK && fb != NULL)
            {
                // Send the frame via GVSP
                ret = gvsp_send_frame(fb);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to send frame via GVSP");
                    // Treat frame send failure as a connection issue
                    gvsp_handle_connection_failure();
                }

                // Return the frame buffer
                camera_return_frame(fb);
            }
            else
            {
                ESP_LOGW(TAG, "Failed to capture frame");
                // Camera failure - not a connection issue, just continue
            }

            // Configurable delay between frames
            float frame_rate = gvcp_get_frame_rate_fps();
            uint32_t frame_delay_ms = (frame_rate > 0.0f) ? (uint32_t)(1000.0f / frame_rate) : 1000;
            vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
        }
        else
        {
            // Not streaming, wait longer
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    vTaskDelete(NULL);
}

// Helper function to set client address (called from GVCP when client connects)
esp_err_t gvsp_set_client_address(struct sockaddr_in *addr)
{
    if (addr == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&client_addr, addr, sizeof(client_addr));

    // Use the port configured by the client via GVCP register, or fall back to GVSP_PORT
    uint32_t configured_port = gvcp_get_scphost_port();
    uint16_t target_port = (configured_port > 0 && configured_port <= 65535) ? (uint16_t)configured_port : GVSP_PORT;

    client_addr.sin_port = htons(target_port);
    client_addr_set = true;
    last_client_activity = esp_log_timestamp();

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr->sin_addr), addr_str, INET_ADDRSTRLEN);
    PROTOCOL_LOG_I(TAG, "GVSP client address set to %s:%d (configured_port=%d, using=%s)",
                   addr_str, target_port, configured_port,
                   (configured_port > 0) ? "configured" : "default");

    return ESP_OK;
}

esp_err_t gvsp_clear_client_address(void)
{
    if (client_addr_set)
    {
        PROTOCOL_LOG_I(TAG, "Clearing GVSP client address and cleaning up connection state");

        // Stop streaming if active
        if (streaming_active)
        {
            gvsp_stop_streaming();
        }

        // Clear client address
        client_addr_set = false;
        memset(&client_addr, 0, sizeof(client_addr));

        // Reset connection state
        last_client_activity = 0;
        connection_failures = 0;
        recovery_mode = false;

        // Update connection status
        gvcp_set_connection_status_bit(2, false); // Clear client connected bit
        gvcp_set_connection_status_bit(3, false); // Clear streaming active bit

        // Clear stream status
        gvcp_set_stream_status(0x0000);

        PROTOCOL_LOG_I(TAG, "GVSP client connection state fully cleaned up");
    }

    return ESP_OK;
}

void gvsp_update_client_activity(void)
{
    if (client_addr_set)
    {
        last_client_activity = esp_log_timestamp();
        // Reset connection failure count on successful activity
        if (connection_failures > 0)
        {
            connection_failures = 0;
            ESP_LOGI(TAG, "Client activity restored, resetting failure count");
        }
        // Exit recovery mode if we're in it
        if (recovery_mode)
        {
            recovery_mode = false;
            ESP_LOGI(TAG, "Exiting recovery mode - client communication restored");
        }
    }
}

// Enhanced connection health monitoring
static esp_err_t gvsp_handle_connection_failure(void)
{
    connection_failures++;
    ESP_LOGW(TAG, "Connection failure #%d (max: %d)", connection_failures, max_connection_failures);

    if (connection_failures >= max_connection_failures)
    {
        if (!recovery_mode)
        {
            ESP_LOGW(TAG, "Max connection failures reached, entering recovery mode");
            recovery_mode = true;
            recovery_start_time = esp_log_timestamp();

            // Clean up current connection
            gvsp_stop_streaming();
            gvsp_clear_client_address();
            gvcp_set_stream_status(0x8000);           // Set error bit
            gvcp_set_connection_status_bit(2, false); // Clear client connected bit
            gvcp_set_connection_status_bit(3, false); // Clear streaming active bit
        }
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t gvsp_check_recovery_timeout(void)
{
    if (recovery_mode)
    {
        uint32_t current_time = esp_log_timestamp();
        if (current_time - recovery_start_time > recovery_timeout_ms)
        {
            ESP_LOGE(TAG, "Recovery timeout exceeded, resetting connection state");
            // Force reset
            connection_failures = 0;
            recovery_mode = false;
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

// Statistics functions
uint32_t gvsp_get_total_packets_sent(void)
{
    return total_packets_sent;
}

uint32_t gvsp_get_total_packet_errors(void)
{
    return total_packet_errors;
}

uint32_t gvsp_get_total_frames_sent(void)
{
    return total_frames_sent;
}

uint32_t gvsp_get_total_frame_errors(void)
{
    return total_frame_errors;
}

// Recovery and connection health functions
uint32_t gvsp_get_connection_failures(void)
{
    return connection_failures;
}

bool gvsp_is_in_recovery_mode(void)
{
    return recovery_mode;
}

uint32_t gvsp_get_time_since_last_activity(void)
{
    if (client_addr_set)
    {
        return esp_log_timestamp() - last_client_activity;
    }
    return 0;
}

esp_err_t gvsp_reset_connection_state(void)
{
    connection_failures = 0;
    recovery_mode = false;
    last_client_activity = esp_log_timestamp();
    ESP_LOGI(TAG, "Connection state manually reset");
    return ESP_OK;
}

// Frame ring buffer information functions
uint32_t gvsp_get_frames_stored_in_ring(void)
{
    if (frame_ring_mutex == NULL)
    {
        return 0;
    }

    uint32_t stored = 0;
    if (xSemaphoreTake(frame_ring_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        stored = frames_stored;
        xSemaphoreGive(frame_ring_mutex);
    }
    return stored;
}

esp_err_t gvsp_resend_frame(uint32_t block_id)
{
    local_camera_fb_t fb_temp;
    esp_err_t err = gvsp_get_frame_from_ring(block_id, &fb_temp);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Cannot resend frame with block_id %d - not found in ring", block_id);
        return err;
    }

    PROTOCOL_LOG_I(TAG, "Resending frame with block_id %d", block_id);

    // Note: We don't store the frame again since it's already in the ring
    // Just send it with the original block_id
    uint32_t original_block_id = block_id;
    block_id = original_block_id; // Temporarily set block_id for transmission

    esp_err_t send_err = gvsp_send_frame(&fb_temp);
    if (send_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to resend frame with block_id %d", original_block_id);
    }

    return send_err;
}

// Frame sequence tracking statistics functions
uint32_t gvsp_get_out_of_order_frames(void)
{
    return out_of_order_frames;
}

uint32_t gvsp_get_lost_frames(void)
{
    return lost_frames;
}

uint32_t gvsp_get_duplicate_frames(void)
{
    return duplicate_frames;
}

uint32_t gvsp_get_expected_frame_sequence(void)
{
    return expected_frame_sequence;
}

uint32_t gvsp_get_last_received_sequence(void)
{
    return last_received_sequence;
}

esp_err_t gvsp_set_sequence_tracking(bool enabled)
{
    sequence_tracking_enabled = enabled;
    if (enabled)
    {
        PROTOCOL_LOG_I(TAG, "Frame sequence tracking enabled");
    }
    else
    {
        PROTOCOL_LOG_I(TAG, "Frame sequence tracking disabled");
    }
    return ESP_OK;
}

bool gvsp_is_sequence_tracking_enabled(void)
{
    return sequence_tracking_enabled;
}

// Comprehensive connection state validation and cleanup
esp_err_t gvsp_validate_connection_state(void)
{
    bool state_inconsistent = false;

    // Check for inconsistent states
    if (streaming_active && !client_addr_set)
    {
        ESP_LOGW(TAG, "Inconsistent state: streaming active but no client address");
        streaming_active = false;
        state_inconsistent = true;
    }

    if (client_addr_set && recovery_mode && (esp_log_timestamp() - recovery_start_time > recovery_timeout_ms))
    {
        ESP_LOGW(TAG, "Recovery mode timeout exceeded, forcing cleanup");
        gvsp_clear_client_address();
        state_inconsistent = true;
    }

    if (!client_addr_set && (connection_failures > 0 || recovery_mode))
    {
        ESP_LOGW(TAG, "No client but failure/recovery state set, cleaning up");
        connection_failures = 0;
        recovery_mode = false;
        state_inconsistent = true;
    }

    if (state_inconsistent)
    {
        ESP_LOGI(TAG, "Connection state validation fixed inconsistencies");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

// Force cleanup of all connection state (emergency use)
esp_err_t gvsp_force_cleanup(void)
{
    ESP_LOGW(TAG, "Force cleanup of all GVSP connection state");

    // Take mutex to ensure atomic cleanup
    if (streaming_mutex && xSemaphoreTake(streaming_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        streaming_active = false;
        client_addr_set = false;
        memset(&client_addr, 0, sizeof(client_addr));
        last_client_activity = 0;
        connection_failures = 0;
        recovery_mode = false;
        block_id = 0;
        packet_id = 0;

        xSemaphoreGive(streaming_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take mutex for force cleanup");
        // Force cleanup anyway
        streaming_active = false;
        client_addr_set = false;
        memset(&client_addr, 0, sizeof(client_addr));
        last_client_activity = 0;
        connection_failures = 0;
        recovery_mode = false;
        block_id = 0;
        packet_id = 0;
    }

    // Clear frame ring buffer
    gvsp_clear_frame_ring();

    // Update connection status
    gvcp_set_connection_status_bit(2, false); // Clear client connected bit
    gvcp_set_connection_status_bit(3, false); // Clear streaming active bit
    gvcp_set_stream_status(0x0000);

    ESP_LOGI(TAG, "Force cleanup completed");
    return ESP_OK;
}

// Multipart payload support implementation
esp_err_t gvsp_send_multipart_frame(local_camera_fb_t *fb)
{
    PROTOCOL_LOG_I(TAG, "Sending multipart frame: block_id=%d, size=%d, %dx%d",
                   block_id, fb->len, fb->width, fb->height);

    // In multipart mode, we send the image as component 0
    esp_err_t err = gvsp_send_component(fb, GVSP_COMPONENT_IMAGE, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send image component in multipart frame");
        return err;
    }

    // TODO: Add metadata component (component 1) support in future enhancement
    // For now, just send the image component to demonstrate multipart capability

    PROTOCOL_LOG_I(TAG, "Multipart frame sent successfully");
    return ESP_OK;
}

esp_err_t gvsp_send_component(local_camera_fb_t *fb, uint8_t component_type, uint16_t component_index)
{
    ESP_LOGI(TAG, "Sending component: type=%d, index=%d, size=%d",
             component_type, component_index, fb->len);

    // Store frame in ring buffer
    if (gvsp_store_frame_in_ring(fb, block_id) != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to store frame in ring buffer");
    }

    // Enhanced payload type for multipart
    uint16_t payload_type = GVSP_PAYLOAD_TYPE_IMAGE;
    if (component_type == GVSP_COMPONENT_METADATA)
    {
        payload_type = GVSP_PAYLOAD_TYPE_CHUNK_DATA;
    }

    // Send leader packet with multipart payload type
    esp_err_t err = gvsp_send_leader_packet_multipart(fb->len, fb->width, fb->height,
                                                      fb->format, payload_type, component_index);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send multipart leader packet");
        return err;
    }

    // Send data packets in chunks
    const uint8_t *data_ptr = fb->buf;
    uint32_t bytes_sent = 0;
    uint32_t configured_packet_size = gvcp_get_packet_size();
    uint32_t packet_delay_us = gvcp_get_packet_delay_us();

    while (bytes_sent < fb->len)
    {
        uint32_t chunk_size = fb->len - bytes_sent;
        if (chunk_size > configured_packet_size)
        {
            chunk_size = configured_packet_size;
        }
        err = gvsp_send_data_packet(data_ptr + bytes_sent, chunk_size);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to send data packet at offset %d", bytes_sent);
            return err;
        }
        bytes_sent += chunk_size;

        // Configurable delay between packets
        if (packet_delay_us > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(packet_delay_us / 1000));
        }
    }

    // Send trailer packet with multipart payload type
    err = gvsp_send_trailer_packet_multipart(fb->height, payload_type, component_index);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send multipart trailer packet");
        return err;
    }

    // Update statistics
    total_frames_sent++;
    block_id++;

    return ESP_OK;
}

// Enhanced leader packet for multipart support
static esp_err_t gvsp_send_leader_packet_multipart(uint32_t frame_size, uint32_t width, uint32_t height,
                                                   int pixel_format, uint16_t payload_type, uint16_t component_index)
{
    uint8_t packet[sizeof(gvsp_header_t) + sizeof(gvsp_leader_data_t)];
    gvsp_header_t *header = (gvsp_header_t *)packet;
    gvsp_leader_data_t *leader_data = (gvsp_leader_data_t *)(packet + sizeof(gvsp_header_t));

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_LEADER;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = htonl(component_index); // Use data[1] for component index in multipart mode

    // Leader data with multipart payload type
    leader_data->flags = htons(component_index);     // Component index in flags field
    leader_data->payload_type = htons(payload_type); // Multipart payload type

    // Get current timestamp
    uint64_t timestamp = esp_timer_get_time();
    leader_data->timestamp_high = htonl((uint32_t)(timestamp >> 32));
    leader_data->timestamp_low = htonl((uint32_t)(timestamp & 0xFFFFFFFF));

    leader_data->pixel_format = htonl(camera_format_to_gvsp_format(pixel_format));
    leader_data->size_x = htonl(width);
    leader_data->size_y = htonl(height);
    leader_data->offset_x = htonl(0);
    leader_data->offset_y = htonl(0);
    leader_data->padding_x = htons(0);
    leader_data->padding_y = htons(0);

    PROTOCOL_LOG_I(TAG, "Sending multipart leader: payload_type=0x%04x, component=%d, size=%dx%d",
                   payload_type, component_index, width, height);

    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(packet), 3);
    if (err == ESP_OK)
    {
        total_packets_sent++;
    }
    else
    {
        total_packet_errors++;
    }

    return err;
}

// Enhanced trailer packet for multipart support
static esp_err_t gvsp_send_trailer_packet_multipart(uint32_t height, uint16_t payload_type, uint16_t component_index)
{
    uint8_t packet[sizeof(gvsp_header_t) + sizeof(gvsp_trailer_data_t)];
    gvsp_header_t *header = (gvsp_header_t *)packet;
    gvsp_trailer_data_t *trailer_data = (gvsp_trailer_data_t *)(packet + sizeof(gvsp_header_t));

    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_TRAILER;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = htonl(component_index); // Component index for multipart

    // Trailer data with multipart payload type
    trailer_data->reserved = htons(component_index);  // Component index in reserved field
    trailer_data->payload_type = htons(payload_type); // Multipart payload type
    trailer_data->size_y = htonl(height);

    PROTOCOL_LOG_I(TAG, "Sending multipart trailer: payload_type=0x%04x, component=%d",
                   payload_type, component_index);

    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(packet), 3);
    if (err == ESP_OK)
    {
        total_packets_sent++;
    }
    else
    {
        total_packet_errors++;
    }

    return err;
}