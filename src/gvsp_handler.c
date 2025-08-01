#include "gvsp_handler.h"
#include "camera_handler.h"
#include "gvcp_handler.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "gvsp_handler";

static int gvsp_sock = -1;
static bool streaming_active = false;
static uint32_t block_id = 0;
static uint16_t packet_id = 0;
static SemaphoreHandle_t streaming_mutex = NULL;
static struct sockaddr_in client_addr;
static bool client_addr_set = false;
static uint32_t last_client_activity = 0;
static uint32_t client_timeout_ms = 30000; // 30 second timeout

// Error handling and statistics
static uint32_t total_packets_sent = 0;
static uint32_t total_packet_errors = 0;
static uint32_t total_frames_sent = 0;
static uint32_t total_frame_errors = 0;

esp_err_t gvsp_init(void)
{
    struct sockaddr_in dest_addr;
    
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(GVSP_PORT);

    gvsp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (gvsp_sock < 0) {
        ESP_LOGE(TAG, "Unable to create GVSP socket: errno %d", errno);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GVSP socket created");

    int err = bind(gvsp_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "GVSP socket unable to bind: errno %d", errno);
        close(gvsp_sock);
        gvsp_sock = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GVSP socket bound to port %d", GVSP_PORT);

    // Create mutex for streaming control
    streaming_mutex = xSemaphoreCreateMutex();
    if (streaming_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create streaming mutex");
        close(gvsp_sock);
        gvsp_sock = -1;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t gvsp_start_streaming(void)
{
    if (xSemaphoreTake(streaming_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    streaming_active = true;
    block_id = 1; // Start with block ID 1
    packet_id = 0;
    
    ESP_LOGI(TAG, "GVSP streaming started");
    
    xSemaphoreGive(streaming_mutex);
    return ESP_OK;
}

esp_err_t gvsp_stop_streaming(void)
{
    if (xSemaphoreTake(streaming_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    streaming_active = false;
    
    // Clean up streaming state
    block_id = 0;
    packet_id = 0;
    
    // Update stream status
    gvcp_set_stream_status(0x0000); // Clear all status bits
    
    ESP_LOGI(TAG, "GVSP streaming stopped and cleaned up");
    
    xSemaphoreGive(streaming_mutex);
    return ESP_OK;
}

bool gvsp_is_streaming(void)
{
    bool is_streaming = false;
    
    if (xSemaphoreTake(streaming_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        is_streaming = streaming_active;
        xSemaphoreGive(streaming_mutex);
    }
    
    return is_streaming;
}

// Helper function to send UDP packet with retry
static esp_err_t gvsp_send_udp_packet(const uint8_t *packet, size_t packet_size, int max_retries)
{
    for (int retry = 0; retry < max_retries; retry++) {
        int err = sendto(gvsp_sock, packet, packet_size, 0,
                        (struct sockaddr *)&client_addr, sizeof(client_addr));
        if (err >= 0) {
            total_packets_sent++;
            return ESP_OK;
        } else {
            total_packet_errors++;
            ESP_LOGW(TAG, "Send failed (attempt %d/%d): errno %d", retry + 1, max_retries, errno);
            
            if (retry < max_retries - 1) {
                vTaskDelay(pdMS_TO_TICKS(1)); // Small delay before retry
            }
        }
    }
    
    ESP_LOGE(TAG, "Failed to send packet after %d attempts", max_retries);
    return ESP_FAIL;
}

static esp_err_t gvsp_send_leader_packet(uint32_t frame_size, uint32_t width, uint32_t height)
{
    uint8_t packet[sizeof(gvsp_header_t) + sizeof(gvsp_leader_data_t)];
    gvsp_header_t *header = (gvsp_header_t*)packet;
    gvsp_leader_data_t *leader_data = (gvsp_leader_data_t*)(packet + sizeof(gvsp_header_t));
    
    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_LEADER;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = 0; // Reserved
    
    // Leader data
    leader_data->flags = htons(0);
    leader_data->payload_type = htons(0x0001); // Image payload type
    leader_data->timestamp_high = 0;
    leader_data->timestamp_low = htonl(esp_log_timestamp());
    leader_data->pixel_format = htonl(GVSP_PIXEL_MONO8);
    leader_data->size_x = htonl(width);
    leader_data->size_y = htonl(height);
    leader_data->offset_x = 0;
    leader_data->offset_y = 0;
    leader_data->padding_x = 0;
    leader_data->padding_y = 0;
    
    if (!client_addr_set) {
        ESP_LOGW(TAG, "No client address set for streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(packet), 3);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Sent GVSP leader packet (%d bytes)", sizeof(packet));
    }
    
    return err;
}

static esp_err_t gvsp_send_data_packet(const uint8_t *data, uint32_t data_size)
{
    uint8_t packet[sizeof(gvsp_header_t) + data_size];
    gvsp_header_t *header = (gvsp_header_t*)packet;
    
    // GVSP header
    header->packet_type = GVSP_PACKET_TYPE_DATA;
    header->flags = 0;
    header->packet_id = htons(packet_id++);
    header->data[0] = htonl(block_id);
    header->data[1] = 0; // Packet data - could be used for data offset
    
    // Copy image data
    memcpy(packet + sizeof(gvsp_header_t), data, data_size);
    
    if (!client_addr_set) {
        ESP_LOGW(TAG, "No client address set for streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(gvsp_header_t) + data_size, 2);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Sent GVSP data packet (%d bytes)", sizeof(gvsp_header_t) + data_size);
    }
    
    return err;
}

static esp_err_t gvsp_send_trailer_packet(uint32_t height)
{
    uint8_t packet[sizeof(gvsp_header_t) + sizeof(gvsp_trailer_data_t)];
    gvsp_header_t *header = (gvsp_header_t*)packet;
    gvsp_trailer_data_t *trailer_data = (gvsp_trailer_data_t*)(packet + sizeof(gvsp_header_t));
    
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
    
    if (!client_addr_set) {
        ESP_LOGW(TAG, "No client address set for streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = gvsp_send_udp_packet(packet, sizeof(packet), 3);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "Sent GVSP trailer packet (%d bytes)", sizeof(packet));
    }
    
    return err;
}

esp_err_t gvsp_send_frame(camera_fb_t *fb)
{
    if (!streaming_active || !client_addr_set) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (fb == NULL || fb->buf == NULL || fb->len == 0) {
        ESP_LOGE(TAG, "Invalid frame buffer");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Sending frame: block_id=%d, size=%d, %dx%d", 
             block_id, fb->len, fb->width, fb->height);
    
    // Send leader packet
    esp_err_t err = gvsp_send_leader_packet(fb->len, fb->width, fb->height);
    if (err != ESP_OK) {
        return err;
    }
    
    // Send data packets
    uint32_t bytes_sent = 0;
    const uint8_t *data_ptr = fb->buf;
    uint32_t configured_packet_size = gvcp_get_packet_size();
    uint32_t packet_delay_us = gvcp_get_packet_delay_us();
    
    while (bytes_sent < fb->len) {
        uint32_t chunk_size = fb->len - bytes_sent;
        if (chunk_size > configured_packet_size) {
            chunk_size = configured_packet_size;
        }
        
        err = gvsp_send_data_packet(data_ptr + bytes_sent, chunk_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send data packet at offset %d", bytes_sent);
            gvcp_set_stream_status(0x8000); // Set error bit
            return err;
        }
        
        bytes_sent += chunk_size;
        
        // Configurable delay between packets
        if (packet_delay_us > 0) {
            vTaskDelay(pdMS_TO_TICKS(packet_delay_us / 1000));
        }
    }
    
    // Send trailer packet
    err = gvsp_send_trailer_packet(fb->height);
    if (err != ESP_OK) {
        total_frame_errors++;
        gvcp_set_stream_status(0x8000); // Set error bit
        return err;
    }
    
    // Increment block ID for next frame
    block_id++;
    total_frames_sent++;
    
    uint32_t total_packets = (fb->len + configured_packet_size - 1) / configured_packet_size + 2;
    ESP_LOGI(TAG, "Frame sent successfully: %d packets, block_id=%d", total_packets, block_id);
    
    // Update stream status with success
    gvcp_set_stream_status(0x0001); // Active streaming bit
    
    return ESP_OK;
}

void gvsp_task(void *pvParameters)
{
    ESP_LOGI(TAG, "GVSP task started");
    
    while (1) {
        // Check for client timeout
        if (client_addr_set) {
            uint32_t current_time = esp_log_timestamp();
            if (current_time - last_client_activity > client_timeout_ms) {
                ESP_LOGW(TAG, "Client timeout detected, stopping stream");
                gvsp_stop_streaming();
                gvsp_clear_client_address();
                gvcp_set_stream_status(0x4000); // Set timeout bit
            }
        }
        
        // Check if streaming is active
        if (gvsp_is_streaming() && client_addr_set) {
            // Capture a frame
            camera_fb_t *fb = NULL;
            esp_err_t ret = camera_capture_frame(&fb);
            
            if (ret == ESP_OK && fb != NULL) {
                // Send the frame via GVSP
                ret = gvsp_send_frame(fb);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to send frame via GVSP");
                }
                
                // Return the frame buffer
                camera_return_frame(fb);
            } else {
                ESP_LOGW(TAG, "Failed to capture frame");
            }
            
            // Configurable delay between frames
            uint32_t frame_rate = gvcp_get_frame_rate_fps();
            uint32_t frame_delay_ms = (frame_rate > 0) ? (1000 / frame_rate) : 1000;
            vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
        } else {
            // Not streaming, wait longer
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    vTaskDelete(NULL);
}

// Helper function to set client address (called from GVCP when client connects)
esp_err_t gvsp_set_client_address(struct sockaddr_in *addr)
{
    if (addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&client_addr, addr, sizeof(client_addr));
    client_addr.sin_port = htons(GVSP_PORT); // Use GVSP port for streaming
    client_addr_set = true;
    last_client_activity = esp_log_timestamp();
    
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr->sin_addr), addr_str, INET_ADDRSTRLEN);
    ESP_LOGI(TAG, "GVSP client address set to %s:%d", addr_str, GVSP_PORT);
    
    return ESP_OK;
}

esp_err_t gvsp_clear_client_address(void)
{
    client_addr_set = false;
    memset(&client_addr, 0, sizeof(client_addr));
    ESP_LOGI(TAG, "GVSP client address cleared");
    
    return ESP_OK;
}

void gvsp_update_client_activity(void)
{
    if (client_addr_set) {
        last_client_activity = esp_log_timestamp();
    }
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