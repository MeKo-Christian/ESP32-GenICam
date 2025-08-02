#include "gvcp_registers.h"
#include "gvcp_protocol.h"
#include "gvcp_bootstrap.h"
#include "gvcp_statistics.h"
#include "gvcp_discovery.h"
#include "camera_handler.h"
#include "gvsp_handler.h"
#include "genicam_xml.h"
#include "status_led.h"
#include "esp_log.h"
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

static const char *TAG = "gvcp_registers";

// Helper function to write register values with proper byte order
static void write_register_value(uint8_t *dest, uint32_t value, size_t size)
{
    uint32_t val_net = htonl(value);
    memcpy(dest, &val_net, 4);
    if (size > 4) {
        memset(dest + 4, 0, size - 4);
    }
}

// External declarations from original handler
extern const uint8_t genicam_xml_data[];
extern const size_t genicam_xml_size;

// Forward declaration of sendto function (to be refactored)
extern esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);

// Stream control parameters
static uint32_t packet_delay_us = 1000; // Inter-packet delay in microseconds (default 1ms)
static uint32_t frame_rate_fps = 1; // Frame rate in FPS (default 1 FPS)
static uint32_t packet_size = 1400; // Data packet size (default 1400)
static uint32_t stream_status = 0; // Stream status register

// Acquisition control state
static uint32_t acquisition_mode = 0; // 0 = Continuous
static uint32_t acquisition_start_reg = 0;
static uint32_t acquisition_stop_reg = 0;

// Forward declaration of sendto function (will need to be refactored)
extern esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);

// Register validation functions
bool is_register_address_valid(uint32_t address)
{
    // Bootstrap region
    if (address < get_bootstrap_memory_size()) {
        return true;
    }
    
    // XML region
    if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size) {
        return true;
    }
    
    // GenICam registers
    if (address >= GENICAM_ACQUISITION_START_OFFSET && address <= GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET) {
        return true;
    }
    
    return false;
}

bool is_register_address_writable(uint32_t address)
{
    // Most bootstrap registers are read-only, except privilege registers
    if (address == GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET ||
        address == GVBS_CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET) {
        return true;
    }
    
    // Most GenICam registers are writable
    if (address >= GENICAM_ACQUISITION_START_OFFSET && address <= GENICAM_TRIGGER_MODE_OFFSET) {
        return true;
    }
    
    // Discovery broadcast control registers
    if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET ||
        address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET) {
        return true;
    }
    
    return false;
}

bool is_bootstrap_register(uint32_t address)
{
    return address < get_bootstrap_memory_size();
}

bool is_genicam_register(uint32_t address)
{
    return address >= GENICAM_ACQUISITION_START_OFFSET && 
           address <= GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET;
}

// Stream configuration getter functions
uint32_t gvcp_get_packet_delay_us(void)
{
    return packet_delay_us;
}

uint32_t gvcp_get_frame_rate_fps(void)
{
    return frame_rate_fps;
}

uint32_t gvcp_get_packet_size(void)
{
    return packet_size;
}

void gvcp_set_stream_status(uint32_t status)
{
    stream_status = status;
}

// NOTE: The complete implementation would include:
// - handle_read_memory_cmd() - ~450 lines of register reading logic
// - handle_write_memory_cmd() - ~300 lines of register writing logic  
// - handle_readreg_cmd() - wrapper for read_memory
// - handle_writereg_cmd() - wrapper for write_memory
// - handle_packetresend_cmd() - GVSP packet resend handling
//
// Due to space constraints, these large functions are not included here.
// The original gvcp_handler.c lines 723-1550 contain the full implementation.
// This modular approach establishes the structure for the refactor.

void handle_read_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size < 8) {
        ESP_LOGE(TAG, "Invalid read memory command size: %d", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer in read memory command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    uint32_t size = ntohl(*(uint32_t*)(data + 4));
    
    ESP_LOGI(TAG, "Read memory: addr=0x%08x, size=%d", address, size);
    
    // Enhanced size validation
    if (size == 0) {
        ESP_LOGW(TAG, "Read memory command with zero size");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    // Allow larger reads for XML content at address 0x10000 and XML region
    uint32_t max_read_size;
    if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size) {
        max_read_size = 8192; // Large reads allowed for XML region
    } else {
        max_read_size = 512;  // Standard reads for other regions
    }
    
    if (size > max_read_size) {
        ESP_LOGW(TAG, "Read size %d exceeds maximum %d for address 0x%08x, clamping", 
                 size, max_read_size, address);
        size = max_read_size;
    }
    
    // Address alignment check for certain registers (4-byte aligned for 32-bit registers)
    bool is_register_access = (address >= GENICAM_ACQUISITION_START_OFFSET && 
                              address <= GENICAM_TRIGGER_MODE_OFFSET);
    if (is_register_access && (address % 4 != 0)) {
        ESP_LOGW(TAG, "Unaligned register access at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }
    
    // Create read memory ACK response - use dynamic allocation for large XML reads
    uint8_t *response;
    uint8_t stack_response[sizeof(gvcp_header_t) + 4 + 512];
    bool use_heap = (size > 512);
    
    if (use_heap) {
        response = malloc(sizeof(gvcp_header_t) + 4 + size);
        if (response == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for read memory response", sizeof(gvcp_header_t) + 4 + size);
            gvcp_send_nack(header, GVCP_ERROR_BUSY, client_addr);
            return;
        }
    } else {
        response = stack_response;
    }
    
    gvcp_header_t *ack_header = (gvcp_header_t*)response;
    
    ack_header->packet_type = GVCP_PACKET_TYPE_ACK;
    ack_header->packet_flags = 0;
    ack_header->command = htons(GVCP_ACK_READ_MEMORY);
    ack_header->size = htons(GVCP_BYTES_TO_WORDS(4 + size));
    ack_header->id = header->id;
    
    // Copy address back
    write_register_value(&response[sizeof(gvcp_header_t)], address, 4);
    
    // Copy memory data
    uint8_t *data_ptr = &response[sizeof(gvcp_header_t) + 4];
    
    if (address < get_bootstrap_memory_size() && address + size <= get_bootstrap_memory_size()) {
        // Bootstrap memory region - handle special registers
        if (address == GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET && size >= 4) {
            // Control Channel Privilege register - return current privilege value
            write_register_value(data_ptr, gvcp_get_control_channel_privilege(), size);
        } else if (address == GVBS_CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET && size >= 4) {
            // Control Channel Privilege Key register - return current key value
            write_register_value(data_ptr, gvcp_get_control_channel_privilege_key(), size);
        } else if (address == GVBS_XML_URL_POINTER_OFFSET && size >= 4) {
            // XML URL pointer register - return address where XML URL string is located
            write_register_value(data_ptr, GVBS_XML_URL_0_OFFSET, size);
        } else {
            // Regular bootstrap memory access
            uint8_t *bootstrap_memory = get_bootstrap_memory();
            memcpy(data_ptr, &bootstrap_memory[address], size);
        }
    } else if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size) {
        // GenICam XML memory region
        uint32_t xml_offset = address - XML_BASE_ADDRESS;
        uint32_t xml_read_size = size;
        
        ESP_LOGI(TAG, "XML read request: addr=0x%08x, offset=%d, requested_size=%d, xml_size=%d", 
                 address, xml_offset, size, genicam_xml_size);
        
        // Validate XML size
        if (genicam_xml_size == 0) {
            ESP_LOGE(TAG, "ERROR: genicam_xml_size is 0!");
            gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
            if (use_heap) free(response);
            return;
        }
        
        if (xml_offset + xml_read_size > genicam_xml_size) {
            xml_read_size = genicam_xml_size - xml_offset;
            ESP_LOGI(TAG, "XML read size clamped to %d bytes", xml_read_size);
        }
        
        if (xml_read_size > 0) {
            memcpy(data_ptr, &genicam_xml_data[xml_offset], xml_read_size);
            ESP_LOGI(TAG, "XML data copied: %d bytes from offset %d", xml_read_size, xml_offset);
            
            // Log first few bytes for debugging
            if (xml_read_size >= 16) {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, data_ptr, 16, ESP_LOG_INFO);
            }
        } else {
            ESP_LOGW(TAG, "XML read size is 0 after bounds checking");
        }
        
        // Fill remaining with zeros if needed
        if (xml_read_size < size) {
            memset(data_ptr + xml_read_size, 0, size - xml_read_size);
            ESP_LOGI(TAG, "Filled %d bytes with zeros after XML data", size - xml_read_size);
        }
    } else if (address == GENICAM_ACQUISITION_START_OFFSET && size >= 4) {
        // Acquisition start register
        write_register_value(data_ptr, acquisition_start_reg, size);
    } else if (address == GENICAM_ACQUISITION_STOP_OFFSET && size >= 4) {
        // Acquisition stop register
        write_register_value(data_ptr, acquisition_stop_reg, size);
    } else if (address == GENICAM_ACQUISITION_MODE_OFFSET && size >= 4) {
        // Acquisition mode register
        write_register_value(data_ptr, acquisition_mode, size);
    } else if (address == GENICAM_PIXEL_FORMAT_OFFSET && size >= 4) {
        // Pixel format register
        write_register_value(data_ptr, camera_get_genicam_pixformat(), size);
    } else if (address == GENICAM_PACKET_DELAY_OFFSET && size >= 4) {
        // Packet delay register (microseconds)
        write_register_value(data_ptr, packet_delay_us, size);
    } else if (address == GENICAM_FRAME_RATE_OFFSET && size >= 4) {
        // Frame rate register (FPS)
        write_register_value(data_ptr, frame_rate_fps, size);
    } else if (address == GENICAM_PACKET_SIZE_OFFSET && size >= 4) {
        // Packet size register
        write_register_value(data_ptr, packet_size, size);
    } else if (address == GENICAM_STREAM_STATUS_OFFSET && size >= 4) {
        // Stream status register
        uint32_t reg_value = htonl(stream_status);
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_PAYLOAD_SIZE_OFFSET && size >= 4) {
        // Payload size register (dynamic based on pixel format)
        uint32_t reg_value = htonl(camera_get_max_payload_size());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_JPEG_QUALITY_OFFSET && size >= 4) {
        // JPEG quality register
        uint32_t reg_value = htonl(camera_get_jpeg_quality());
        memcpy(data_ptr, &reg_value, 4);
        if (size > 4) {
            memset(data_ptr + 4, 0, size - 4);
        }
    } else if (address == GENICAM_EXPOSURE_TIME_OFFSET && size >= 4) {
        // Exposure time register (microseconds)
        write_register_value(data_ptr, camera_get_exposure_time(), size);
    } else if (address == GENICAM_GAIN_OFFSET && size >= 4) {
        // Gain register (dB)
        write_register_value(data_ptr, (uint32_t)camera_get_gain(), size);
    } else if (address == GENICAM_BRIGHTNESS_OFFSET && size >= 4) {
        // Brightness register (-2 to +2)
        write_register_value(data_ptr, (uint32_t)camera_get_brightness(), size);
    } else if (address == GENICAM_CONTRAST_OFFSET && size >= 4) {
        // Contrast register (-2 to +2)
        write_register_value(data_ptr, (uint32_t)camera_get_contrast(), size);
    } else if (address == GENICAM_SATURATION_OFFSET && size >= 4) {
        // Saturation register (-2 to +2)
        write_register_value(data_ptr, (uint32_t)camera_get_saturation(), size);
    } else if (address == GENICAM_WHITE_BALANCE_MODE_OFFSET && size >= 4) {
        // White balance mode register
        write_register_value(data_ptr, (uint32_t)camera_get_white_balance_mode(), size);
    } else if (address == GENICAM_TRIGGER_MODE_OFFSET && size >= 4) {
        // Trigger mode register
        write_register_value(data_ptr, (uint32_t)camera_get_trigger_mode(), size);
    } else if (address == GENICAM_TOTAL_COMMANDS_OFFSET && size >= 4) {
        // Total commands received register
        write_register_value(data_ptr, gvcp_get_total_commands_received(), size);
    } else if (address == GENICAM_TOTAL_ERRORS_OFFSET && size >= 4) {
        // Total errors sent register
        write_register_value(data_ptr, gvcp_get_total_errors_sent(), size);
    } else if (address == GENICAM_UNKNOWN_COMMANDS_OFFSET && size >= 4) {
        // Unknown commands register
        write_register_value(data_ptr, gvcp_get_total_unknown_commands(), size);
    } else if (address == GENICAM_PACKETS_SENT_OFFSET && size >= 4) {
        // Total packets sent register (from GVSP)
        write_register_value(data_ptr, gvsp_get_total_packets_sent(), size);
    } else if (address == GENICAM_PACKET_ERRORS_OFFSET && size >= 4) {
        // Packet errors register (from GVSP)
        write_register_value(data_ptr, gvsp_get_total_packet_errors(), size);
    } else if (address == GENICAM_FRAMES_SENT_OFFSET && size >= 4) {
        // Total frames sent register (from GVSP)
        write_register_value(data_ptr, gvsp_get_total_frames_sent(), size);
    } else if (address == GENICAM_FRAME_ERRORS_OFFSET && size >= 4) {
        // Frame errors register (from GVSP)
        write_register_value(data_ptr, gvsp_get_total_frame_errors(), size);
    } else if (address == GENICAM_CONNECTION_STATUS_OFFSET && size >= 4) {
        // Connection status register
        write_register_value(data_ptr, gvcp_get_connection_status(), size);
    } else if (address == GENICAM_OUT_OF_ORDER_FRAMES_OFFSET && size >= 4) {
        // Out-of-order frames register
        write_register_value(data_ptr, gvsp_get_out_of_order_frames(), size);
    } else if (address == GENICAM_LOST_FRAMES_OFFSET && size >= 4) {
        // Lost frames register
        write_register_value(data_ptr, gvsp_get_lost_frames(), size);
    } else if (address == GENICAM_DUPLICATE_FRAMES_OFFSET && size >= 4) {
        // Duplicate frames register
        write_register_value(data_ptr, gvsp_get_duplicate_frames(), size);
    } else if (address == GENICAM_EXPECTED_SEQUENCE_OFFSET && size >= 4) {
        // Expected sequence register
        write_register_value(data_ptr, gvsp_get_expected_frame_sequence(), size);
    } else if (address == GENICAM_LAST_SEQUENCE_OFFSET && size >= 4) {
        // Last received sequence register
        write_register_value(data_ptr, gvsp_get_last_received_sequence(), size);
    } else if (address == GENICAM_FRAMES_IN_RING_OFFSET && size >= 4) {
        // Frames in ring buffer register
        write_register_value(data_ptr, gvsp_get_frames_stored_in_ring(), size);
    } else if (address == GENICAM_CONNECTION_FAILURES_OFFSET && size >= 4) {
        // Connection failures register
        write_register_value(data_ptr, gvsp_get_connection_failures(), size);
    } else if (address == GENICAM_RECOVERY_MODE_OFFSET && size >= 4) {
        // Recovery mode register (0 = false, 1 = true)
        write_register_value(data_ptr, gvsp_is_in_recovery_mode() ? 1 : 0, size);
    } else if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET && size >= 4) {
        // Discovery broadcast enable register (0 = disabled, 1 = enabled)
        write_register_value(data_ptr, gvcp_get_discovery_broadcasts_sent() ? 1 : 0, size); // Using getter from discovery module
    } else if (address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET && size >= 4) {
        // Discovery broadcast interval register (milliseconds) - using default
        write_register_value(data_ptr, 5000, size); // Default interval
    } else if (address == GENICAM_DISCOVERY_BROADCASTS_SENT_OFFSET && size >= 4) {
        // Discovery broadcasts sent register
        write_register_value(data_ptr, gvcp_get_discovery_broadcasts_sent(), size);
    } else if (address == GENICAM_DISCOVERY_BROADCAST_FAILURES_OFFSET && size >= 4) {
        // Discovery broadcast failures register
        write_register_value(data_ptr, gvcp_get_discovery_broadcast_failures(), size);
    } else if (address == GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET && size >= 4) {
        // Discovery broadcast sequence register
        write_register_value(data_ptr, gvcp_get_discovery_broadcast_sequence(), size);
    } else {
        // Unknown memory region - return zeros
        memset(data_ptr, 0, size);
    }
    
    // Send response
    size_t response_size = sizeof(gvcp_header_t) + 4 + size;
    esp_err_t err = gvcp_sendto(response, response_size, client_addr);
    
    // Cleanup heap allocation if used
    if (use_heap) {
        free(response);
    }
    
    // Update client activity
    gvsp_update_client_activity();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending read memory ACK");
    } else {
        ESP_LOGI(TAG, "Sent read memory ACK: %d total bytes, payload=%d bytes (%d words), data_len=%d", 
             response_size, (4 + size), (4 + size) / 4, size);
    }
}

void handle_write_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size < 4) {
        ESP_LOGE(TAG, "Invalid write memory command size: %d", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer in write memory command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    uint32_t size = packet_size - 4;
    const uint8_t *write_data = data + 4;
    
    ESP_LOGI(TAG, "Write memory: addr=0x%08x, size=%d", address, size);
    
    // Enhanced write validation
    if (size == 0) {
        ESP_LOGW(TAG, "Write memory command with zero size");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (size > 512) {
        ESP_LOGW(TAG, "Write size %d exceeds maximum", size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    // Address alignment check for register access
    bool is_register_access = (address >= GENICAM_ACQUISITION_START_OFFSET && 
                              address <= GENICAM_JPEG_QUALITY_OFFSET);
    if (is_register_access && (address % 4 != 0)) {
        ESP_LOGW(TAG, "Unaligned register write at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }
    
    // Handle different write regions
    
    if (address >= GVBS_USER_DEFINED_NAME_OFFSET && 
        address + size <= GVBS_USER_DEFINED_NAME_OFFSET + 16) {
        // Bootstrap user-defined name
        uint8_t *bootstrap_memory = get_bootstrap_memory();
        memcpy(&bootstrap_memory[address], write_data, size);
        ESP_LOGI(TAG, "Write successful to bootstrap address 0x%08x", address);
    } else if (address == GENICAM_ACQUISITION_START_OFFSET && size >= 4) {
        // Acquisition start command
        uint32_t command_value = ntohl(*(uint32_t*)write_data);
        if (command_value == 1) {
            ESP_LOGI(TAG, "Acquisition start command received");
            status_led_set_state(LED_STATE_FAST_BLINK);
            gvsp_start_streaming();
            acquisition_start_reg = 1;
            // Set streaming active bit
            gvcp_set_connection_status_bit(GVCP_CONNECTION_STATUS_STREAMING, true);
        }
    } else if (address == GENICAM_ACQUISITION_STOP_OFFSET && size >= 4) {
        // Acquisition stop command
        uint32_t command_value = ntohl(*(uint32_t*)write_data);
        if (command_value == 1) {
            ESP_LOGI(TAG, "Acquisition stop command received");
            status_led_set_state(LED_STATE_ON);
            gvsp_stop_streaming();
            gvsp_clear_client_address();
            acquisition_stop_reg = 1;
            // Clear streaming active bit and client connected bit
            gvcp_set_connection_status_bit(GVCP_CONNECTION_STATUS_STREAMING, false);
            gvcp_set_connection_status_bit(GVCP_CONNECTION_STATUS_CLIENT_CONN, false);
        }
    } else if (address == GENICAM_ACQUISITION_MODE_OFFSET && size >= 4) {
        // Acquisition mode
        acquisition_mode = ntohl(*(uint32_t*)write_data);
        ESP_LOGI(TAG, "Acquisition mode set to: %d", acquisition_mode);
    } else if (address == GENICAM_PIXEL_FORMAT_OFFSET && size >= 4) {
        // Pixel format
        uint32_t format_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_genicam_pixformat(format_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Pixel format set to: 0x%08X", format_value);
            } else {
            ESP_LOGW(TAG, "Failed to set pixel format: 0x%08X", format_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_PACKET_DELAY_OFFSET && size >= 4) {
        // Packet delay (microseconds)
        uint32_t new_delay = ntohl(*(uint32_t*)write_data);
        if (new_delay >= 100 && new_delay <= 100000) { // 0.1ms to 100ms range
            packet_delay_us = new_delay;
            ESP_LOGI(TAG, "Packet delay set to: %d microseconds", packet_delay_us);
            } else {
            ESP_LOGW(TAG, "Packet delay %d out of range (100-100000 us)", new_delay);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_FRAME_RATE_OFFSET && size >= 4) {
        // Frame rate (FPS)
        uint32_t new_fps = ntohl(*(uint32_t*)write_data);
        if (new_fps >= 1 && new_fps <= 30) { // 1-30 FPS range
            frame_rate_fps = new_fps;
            ESP_LOGI(TAG, "Frame rate set to: %d FPS", frame_rate_fps);
            } else {
            ESP_LOGW(TAG, "Frame rate %d out of range (1-30 FPS)", new_fps);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_PACKET_SIZE_OFFSET && size >= 4) {
        // Packet size
        uint32_t new_size = ntohl(*(uint32_t*)write_data);
        if (new_size >= 512 && new_size <= GVSP_DATA_PACKET_SIZE) {
            packet_size = new_size;
            ESP_LOGI(TAG, "Packet size set to: %d bytes", packet_size);
            } else {
            ESP_LOGW(TAG, "Packet size %d out of range (512-%d bytes)", new_size, GVSP_DATA_PACKET_SIZE);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_JPEG_QUALITY_OFFSET && size >= 4) {
        // JPEG quality
        uint32_t quality_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_jpeg_quality(quality_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "JPEG quality set to: %d", quality_value);
            } else {
            ESP_LOGW(TAG, "Failed to set JPEG quality: %d", quality_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_EXPOSURE_TIME_OFFSET && size >= 4) {
        // Exposure time
        uint32_t exposure_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_exposure_time(exposure_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Exposure time set to: %lu us", exposure_value);
            } else {
            ESP_LOGW(TAG, "Failed to set exposure time: %lu us", exposure_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_GAIN_OFFSET && size >= 4) {
        // Gain
        uint32_t gain_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_gain((int)gain_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Gain set to: %d dB", (int)gain_value);
            } else {
            ESP_LOGW(TAG, "Failed to set gain: %d dB", (int)gain_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_BRIGHTNESS_OFFSET && size >= 4) {
        // Brightness
        int32_t brightness_value = (int32_t)ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_brightness(brightness_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Brightness set to: %d", brightness_value);
            } else {
            ESP_LOGW(TAG, "Failed to set brightness: %d", brightness_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_CONTRAST_OFFSET && size >= 4) {
        // Contrast
        int32_t contrast_value = (int32_t)ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_contrast(contrast_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Contrast set to: %d", contrast_value);
            } else {
            ESP_LOGW(TAG, "Failed to set contrast: %d", contrast_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_SATURATION_OFFSET && size >= 4) {
        // Saturation
        int32_t saturation_value = (int32_t)ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_saturation(saturation_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Saturation set to: %d", saturation_value);
            } else {
            ESP_LOGW(TAG, "Failed to set saturation: %d", saturation_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_WHITE_BALANCE_MODE_OFFSET && size >= 4) {
        // White balance mode
        uint32_t wb_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_white_balance_mode((int)wb_value);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "White balance mode set to: %s", wb_value == 0 ? "OFF" : "AUTO");
            } else {
            ESP_LOGW(TAG, "Failed to set white balance mode: %d", (int)wb_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_TRIGGER_MODE_OFFSET && size >= 4) {
        // Trigger mode
        uint32_t trigger_value = ntohl(*(uint32_t*)write_data);
        esp_err_t ret = camera_set_trigger_mode((int)trigger_value);
        if (ret == ESP_OK) {
            const char* mode_str = trigger_value == 0 ? "OFF" : 
                                  trigger_value == 1 ? "ON" : "SOFTWARE";
            ESP_LOGI(TAG, "Trigger mode set to: %s", mode_str);
            } else {
            ESP_LOGW(TAG, "Failed to set trigger mode: %d", (int)trigger_value);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    } else if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET && size >= 4) {
        // Discovery broadcast enable/disable
        uint32_t enable_value = ntohl(*(uint32_t*)write_data);
        bool enable = (enable_value != 0);
        gvcp_enable_discovery_broadcast(enable);
        ESP_LOGI(TAG, "Discovery broadcast %s", enable ? "enabled" : "disabled");
    } else if (address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET && size >= 4) {
        // Discovery broadcast interval
        uint32_t interval_value = ntohl(*(uint32_t*)write_data);
        gvcp_set_discovery_broadcast_interval(interval_value);
        ESP_LOGI(TAG, "Discovery broadcast interval set to %d ms", interval_value);
    } else {
        ESP_LOGW(TAG, "Write denied to address 0x%08x (read-only or out of range)", address);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
        return;
    }
    
    // Create write memory ACK response
    uint8_t response[sizeof(gvcp_header_t) + 4];
    gvcp_header_t *ack_header = (gvcp_header_t*)response;
    
    ack_header->packet_type = GVCP_PACKET_TYPE_ACK;
    ack_header->packet_flags = 0;
    ack_header->command = htons(GVCP_ACK_WRITE_MEMORY);
    ack_header->size = htons(GVCP_BYTES_TO_WORDS(4)); // 4 bytes payload (address) = 1 word
    ack_header->id = header->id;
    
    // Copy address back
    *(uint32_t*)&response[sizeof(gvcp_header_t)] = htonl(address);
    
    // Send response
    esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);
    
    // Update client activity
    gvsp_update_client_activity();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending write memory ACK");
    } else {
        ESP_LOGI(TAG, "Sent write memory ACK");
    }
}

void handle_readreg_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size != 4) {
        ESP_LOGE(TAG, "Invalid read register command size: %d (expected 4)", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer in read register command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    
    ESP_LOGI(TAG, "Read register: addr=0x%08x", address);
    
    // Address alignment check for 32-bit registers
    if (address % 4 != 0) {
        ESP_LOGW(TAG, "Unaligned register access at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }
    
    // Delegate to existing read memory logic with 4-byte size for register reads
    // Create a temporary data buffer with address and size
    uint8_t read_memory_data[8];
    write_register_value(&read_memory_data[0], address, 4);  // Address
    write_register_value(&read_memory_data[4], 4, 4);       // Size = 4 bytes
    
    // Create a temporary header with READ_MEMORY command and adjusted size
    gvcp_header_t temp_header = *header;
    temp_header.command = htons(GVCP_CMD_READ_MEMORY);
    temp_header.size = htons(8);  // Address (4) + Size (4)
    
    // Use existing read memory logic
    handle_read_memory_cmd(&temp_header, read_memory_data, client_addr);
    
    // Note: The response will have READ_MEMORY ACK command code, which is acceptable
    // since READREG is essentially a simplified READ_MEMORY operation
}

void handle_writereg_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size != 8) {
        ESP_LOGE(TAG, "Invalid write register command size: %d (expected 8)", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer in write register command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    uint32_t address = ntohl(*(uint32_t*)data);
    uint32_t value = ntohl(*(uint32_t*)(data + 4));
    
    ESP_LOGI(TAG, "Write register: addr=0x%08x, value=0x%08x", address, value);
    
    // Address alignment check for 32-bit registers
    if (address % 4 != 0) {
        ESP_LOGW(TAG, "Unaligned register write at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }
    
    // Delegate to existing write memory logic with 4-byte value for register writes
    // Create a temporary data buffer with address and value in network byte order
    uint8_t write_memory_data[8];
    *(uint32_t*)&write_memory_data[0] = htonl(address);
    *(uint32_t*)&write_memory_data[4] = htonl(value);
    
    // Create a temporary header with WRITE_MEMORY command and adjusted size
    gvcp_header_t temp_header = *header;
    temp_header.command = htons(GVCP_CMD_WRITE_MEMORY);
    temp_header.size = htons(8);  // Address (4) + Value (4)
    
    // Use existing write memory logic
    handle_write_memory_cmd(&temp_header, write_memory_data, client_addr);
    
    // Note: The response will have WRITE_MEMORY ACK command code, which is acceptable
    // since WRITEREG is essentially a simplified WRITE_MEMORY operation
}

void handle_packetresend_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size < 8) {
        ESP_LOGE(TAG, "Invalid packet resend command size: %d (minimum 8)", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer in packet resend command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    // Extract resend parameters - standard GVCP packet resend format
    uint32_t stream_channel_index = ntohl(*(uint32_t*)data);
    uint32_t block_id = ntohl(*(uint32_t*)(data + 4));
    
    ESP_LOGI(TAG, "Packet resend request: stream=%d, block_id=%d", stream_channel_index, block_id);
    
    // Validate stream channel (we only support channel 0)
    if (stream_channel_index != 0) {
        ESP_LOGW(TAG, "Invalid stream channel index: %d", stream_channel_index);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    // Check if streaming is active
    if (!gvsp_is_streaming()) {
        ESP_LOGW(TAG, "Packet resend requested but streaming is not active");
        gvcp_send_nack(header, GVCP_ERROR_WRONG_CONFIG, client_addr);
        return;
    }
    
    // Attempt to resend the requested frame/block
    esp_err_t resend_result = gvsp_resend_frame(block_id);
    
    if (resend_result != ESP_OK) {
        ESP_LOGW(TAG, "Failed to resend block_id %d: %s", block_id, esp_err_to_name(resend_result));
        // Send NACK for unavailable data
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }
    
    // Create packet resend ACK response
    uint8_t response[sizeof(gvcp_header_t) + 8];
    gvcp_header_t *ack_header = (gvcp_header_t*)response;
    
    ack_header->packet_type = GVCP_PACKET_TYPE_ACK;
    ack_header->packet_flags = 0;
    ack_header->command = htons(GVCP_ACK_PACKETRESEND);
    ack_header->size = htons(GVCP_BYTES_TO_WORDS(8));  // 8 bytes payload (stream channel + block ID) = 2 words
    ack_header->id = header->id;
    
    // Echo back the resend parameters
    write_register_value(&response[sizeof(gvcp_header_t)], stream_channel_index, 4);
    write_register_value(&response[sizeof(gvcp_header_t) + 4], block_id, 4);
    
    // Send response
    esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);
    
    // Update client activity
    gvsp_update_client_activity();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error sending packet resend ACK");
    } else {
        ESP_LOGI(TAG, "Sent packet resend ACK for block_id %d", block_id);
    }
}

esp_err_t gvcp_registers_init(void)
{
    // Initialize stream parameters
    packet_delay_us = 1000;
    frame_rate_fps = 1; 
    packet_size = 1400;
    stream_status = 0;
    
    // Initialize acquisition state
    acquisition_mode = 0;
    acquisition_start_reg = 0;
    acquisition_stop_reg = 0;
    
    ESP_LOGI(TAG, "Register access module initialized");
    return ESP_OK;
}