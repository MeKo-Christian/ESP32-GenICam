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
#include "esp_err.h"
#include <sys/param.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

static const char *TAG = "gvcp_registers";

// Helper function to write register values with proper byte order
static void write_register_value(uint8_t *dest, uint32_t value, size_t size)
{
    uint32_t val_net = htonl(value);
    memcpy(dest, &val_net, 4);
    if (size > 4)
    {
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
static uint32_t frame_rate_fps = 1;     // Frame rate in FPS (default 1 FPS)
static uint32_t packet_size = 1400;     // Data packet size (default 1400)
static uint32_t stream_status = 0;      // Stream status register

// Acquisition control state
static uint32_t acquisition_mode = 0; // 0 = Continuous
static uint32_t acquisition_start_reg = 0;
static uint32_t acquisition_stop_reg = 0;

// Standard GVCP registers for Aravis compatibility
static uint32_t tl_params_locked = 0;        // 0x0A00 - TLParamsLocked
static uint32_t stream_dest_address = 0;     // 0x0A10 - GevSCDA (destination IP)

// Forward declaration of sendto function (will need to be refactored)
extern esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);

// Register validation functions
bool is_register_address_valid(uint32_t address)
{
    // Bootstrap region
    if (address < get_bootstrap_memory_size())
    {
        return true;
    }

    // XML region
    if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size)
    {
        return true;
    }

    // GenICam registers
    if (address >= GENICAM_ACQUISITION_START_OFFSET && address <= GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET)
    {
        return true;
    }

    // Standard GVCP registers (0x0A00-0x0A10 range)
    if (address >= GVCP_TL_PARAMS_LOCKED_OFFSET && address <= GVCP_GEVSCDA_DEST_ADDRESS_OFFSET)
    {
        return true;
    }

    return false;
}

bool is_register_address_writable(uint32_t address)
{
    // Most bootstrap registers are read-only, except privilege registers
    if (address == GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET ||
        address == GVBS_CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET)
    {
        return true;
    }

    // Most GenICam registers are writable
    if (address >= GENICAM_ACQUISITION_START_OFFSET && address <= GENICAM_TRIGGER_MODE_OFFSET)
    {
        return true;
    }

    // Discovery broadcast control registers
    if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET ||
        address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET)
    {
        return true;
    }

    // Standard GVCP registers - all writable
    if (address == GVCP_TL_PARAMS_LOCKED_OFFSET ||
        address == GVCP_GEVSCPS_PACKET_SIZE_OFFSET ||
        address == GVCP_GEVSCPD_PACKET_DELAY_OFFSET ||
        address == GVCP_GEVSCDA_DEST_ADDRESS_OFFSET)
    {
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

// Standard GVCP register getter/setter functions
uint32_t gvcp_get_tl_params_locked(void)
{
    return tl_params_locked;
}

void gvcp_set_tl_params_locked(uint32_t locked)
{
    tl_params_locked = locked;
}

uint32_t gvcp_get_stream_dest_address(void)
{
    return stream_dest_address;
}

void gvcp_set_stream_dest_address(uint32_t dest_ip)
{
    stream_dest_address = dest_ip;
}

// Minimal helper for register-value resolution without sending packets
static bool handle_read_memory_cmd_inline(uint32_t address, uint32_t size, uint8_t *out)
{
    if (size < 4 || out == NULL)
    {
        return false;
    }

    // Bootstrap registers
    if (address < get_bootstrap_memory_size())
    {
        if (address == GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET)
        {
            write_register_value(out, gvcp_get_control_channel_privilege(), size);
        }
        else if (address == GVBS_CONTROL_CHANNEL_PRIVILEGE_KEY_OFFSET)
        {
            write_register_value(out, gvcp_get_control_channel_privilege_key(), size);
        }
        else if (address == GVBS_XML_URL_POINTER_OFFSET)
        {
            write_register_value(out, GVBS_XML_URL_0_OFFSET, size);
        }
        else
        {
            uint8_t *bootstrap = get_bootstrap_memory();
            memcpy(out, &bootstrap[address], size);
        }
        return true;
    }

    // XML region (read not used in READREG, but included for completeness)
    if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size)
    {
        uint32_t offset = address - XML_BASE_ADDRESS;
        uint32_t read_size = MIN(size, genicam_xml_size - offset);
        memcpy(out, &genicam_xml_data[offset], read_size);
        if (read_size < size)
        {
            memset(out + read_size, 0, size - read_size);
        }
        return true;
    }

    // GenICam registers
    if (address == GENICAM_ACQUISITION_START_OFFSET)
    {
        write_register_value(out, acquisition_start_reg, size);
    }
    else if (address == GENICAM_ACQUISITION_STOP_OFFSET)
    {
        write_register_value(out, acquisition_stop_reg, size);
    }
    else if (address == GENICAM_ACQUISITION_MODE_OFFSET)
    {
        write_register_value(out, acquisition_mode, size);
    }
    else if (address == GENICAM_PIXEL_FORMAT_OFFSET)
    {
        write_register_value(out, camera_get_genicam_pixformat(), size);
    }
    else if (address == GENICAM_PACKET_DELAY_OFFSET)
    {
        write_register_value(out, packet_delay_us, size);
    }
    else if (address == GENICAM_FRAME_RATE_OFFSET)
    {
        write_register_value(out, frame_rate_fps, size);
    }
    else if (address == GENICAM_PACKET_SIZE_OFFSET)
    {
        write_register_value(out, packet_size, size);
    }
    else if (address == GENICAM_STREAM_STATUS_OFFSET)
    {
        write_register_value(out, stream_status, size);
    }
    else if (address == GENICAM_PAYLOAD_SIZE_OFFSET)
    {
        write_register_value(out, camera_get_max_payload_size(), size);
    }
    else if (address == GENICAM_JPEG_QUALITY_OFFSET)
    {
        write_register_value(out, camera_get_jpeg_quality(), size);
    }
    else if (address == GENICAM_EXPOSURE_TIME_OFFSET)
    {
        write_register_value(out, camera_get_exposure_time(), size);
    }
    else if (address == GENICAM_GAIN_OFFSET)
    {
        write_register_value(out, (uint32_t)camera_get_gain(), size);
    }
    else if (address == GENICAM_BRIGHTNESS_OFFSET)
    {
        write_register_value(out, (uint32_t)camera_get_brightness(), size);
    }
    else if (address == GENICAM_CONTRAST_OFFSET)
    {
        write_register_value(out, (uint32_t)camera_get_contrast(), size);
    }
    else if (address == GENICAM_SATURATION_OFFSET)
    {
        write_register_value(out, (uint32_t)camera_get_saturation(), size);
    }
    else if (address == GENICAM_WHITE_BALANCE_MODE_OFFSET)
    {
        write_register_value(out, (uint32_t)camera_get_white_balance_mode(), size);
    }
    else if (address == GENICAM_TRIGGER_MODE_OFFSET)
    {
        write_register_value(out, (uint32_t)camera_get_trigger_mode(), size);
    }
    else if (address == GENICAM_TOTAL_COMMANDS_OFFSET)
    {
        write_register_value(out, gvcp_get_total_commands_received(), size);
    }
    else if (address == GENICAM_TOTAL_ERRORS_OFFSET)
    {
        write_register_value(out, gvcp_get_total_errors_sent(), size);
    }
    else if (address == GENICAM_UNKNOWN_COMMANDS_OFFSET)
    {
        write_register_value(out, gvcp_get_total_unknown_commands(), size);
    }
    else if (address == GENICAM_PACKETS_SENT_OFFSET)
    {
        write_register_value(out, gvsp_get_total_packets_sent(), size);
    }
    else if (address == GENICAM_PACKET_ERRORS_OFFSET)
    {
        write_register_value(out, gvsp_get_total_packet_errors(), size);
    }
    else if (address == GENICAM_FRAMES_SENT_OFFSET)
    {
        write_register_value(out, gvsp_get_total_frames_sent(), size);
    }
    else if (address == GENICAM_FRAME_ERRORS_OFFSET)
    {
        write_register_value(out, gvsp_get_total_frame_errors(), size);
    }
    else if (address == GENICAM_CONNECTION_STATUS_OFFSET)
    {
        write_register_value(out, gvcp_get_connection_status(), size);
    }
    else if (address == GENICAM_OUT_OF_ORDER_FRAMES_OFFSET)
    {
        write_register_value(out, gvsp_get_out_of_order_frames(), size);
    }
    else if (address == GENICAM_LOST_FRAMES_OFFSET)
    {
        write_register_value(out, gvsp_get_lost_frames(), size);
    }
    else if (address == GENICAM_DUPLICATE_FRAMES_OFFSET)
    {
        write_register_value(out, gvsp_get_duplicate_frames(), size);
    }
    else if (address == GENICAM_EXPECTED_SEQUENCE_OFFSET)
    {
        write_register_value(out, gvsp_get_expected_frame_sequence(), size);
    }
    else if (address == GENICAM_LAST_SEQUENCE_OFFSET)
    {
        write_register_value(out, gvsp_get_last_received_sequence(), size);
    }
    else if (address == GENICAM_FRAMES_IN_RING_OFFSET)
    {
        write_register_value(out, gvsp_get_frames_stored_in_ring(), size);
    }
    else if (address == GENICAM_CONNECTION_FAILURES_OFFSET)
    {
        write_register_value(out, gvsp_get_connection_failures(), size);
    }
    else if (address == GENICAM_RECOVERY_MODE_OFFSET)
    {
        write_register_value(out, gvsp_is_in_recovery_mode() ? 1 : 0, size);
    }
    else if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET)
    {
        write_register_value(out, gvcp_get_discovery_broadcasts_sent() ? 1 : 0, size);
    }
    else if (address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET)
    {
        write_register_value(out, 5000, size);
    }
    else if (address == GENICAM_DISCOVERY_BROADCASTS_SENT_OFFSET)
    {
        write_register_value(out, gvcp_get_discovery_broadcasts_sent(), size);
    }
    else if (address == GENICAM_DISCOVERY_BROADCAST_FAILURES_OFFSET)
    {
        write_register_value(out, gvcp_get_discovery_broadcast_failures(), size);
    }
    else if (address == GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET)
    {
        write_register_value(out, gvcp_get_discovery_broadcast_sequence(), size);
    }
    // Standard GVCP registers
    else if (address == GVCP_TL_PARAMS_LOCKED_OFFSET)
    {
        write_register_value(out, gvcp_get_tl_params_locked(), size);
    }
    else if (address == GVCP_GEVSCPS_PACKET_SIZE_OFFSET)
    {
        write_register_value(out, packet_size, size);
    }
    else if (address == GVCP_GEVSCPD_PACKET_DELAY_OFFSET)
    {
        write_register_value(out, packet_delay_us, size);
    }
    else if (address == GVCP_GEVSCDA_DEST_ADDRESS_OFFSET)
    {
        write_register_value(out, gvcp_get_stream_dest_address(), size);
    }
    else
    {
        memset(out, 0, size);
    }

    return true;
}

static esp_err_t handle_write_memory_cmd_inline(uint32_t address, uint32_t value)
{
    // Bootstrap writable register: User-defined name (example range)
    if (address >= GVBS_USER_DEFINED_NAME_OFFSET &&
        address + 4 <= GVBS_USER_DEFINED_NAME_OFFSET + 16)
    {
        uint8_t *bootstrap = get_bootstrap_memory();
        *(uint32_t *)&bootstrap[address] = htonl(value);
        return ESP_OK;
    }

    // Acquisition control
    if (address == GENICAM_ACQUISITION_START_OFFSET)
    {
        if (value == 1)
        {
            status_led_set_state(LED_STATE_FAST_BLINK);
            gvsp_start_streaming();
            acquisition_start_reg = 1;
            gvcp_set_connection_status_bit(GVCP_CONNECTION_STATUS_STREAMING, true);
        }
        return ESP_OK;
    }

    if (address == GENICAM_ACQUISITION_STOP_OFFSET)
    {
        if (value == 1)
        {
            status_led_set_state(LED_STATE_ON);
            gvsp_stop_streaming();
            gvsp_clear_client_address();
            acquisition_stop_reg = 1;
            gvcp_set_connection_status_bit(GVCP_CONNECTION_STATUS_STREAMING, false);
            gvcp_set_connection_status_bit(GVCP_CONNECTION_STATUS_CLIENT_CONN, false);
        }
        return ESP_OK;
    }

    if (address == GENICAM_ACQUISITION_MODE_OFFSET)
    {
        acquisition_mode = value;
        return ESP_OK;
    }

    // Control channel privilege
    if (address == GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET)
    {
        gvcp_set_control_channel_privilege(value);
        return ESP_OK;
    }

    // Pixel format
    if (address == GENICAM_PIXEL_FORMAT_OFFSET)
    {
        return camera_set_genicam_pixformat(value);
    }

    // Packet delay
    if (address == GENICAM_PACKET_DELAY_OFFSET && value >= 100 && value <= 100000)
    {
        packet_delay_us = value;
        return ESP_OK;
    }

    // Frame rate
    if (address == GENICAM_FRAME_RATE_OFFSET && value >= 1 && value <= 30)
    {
        frame_rate_fps = value;
        return ESP_OK;
    }

    // Packet size
    if (address == GENICAM_PACKET_SIZE_OFFSET && value >= 512 && value <= GVSP_DATA_PACKET_SIZE)
    {
        packet_size = value;
        return ESP_OK;
    }

    // JPEG quality
    if (address == GENICAM_JPEG_QUALITY_OFFSET)
    {
        return camera_set_jpeg_quality(value);
    }

    // Exposure time
    if (address == GENICAM_EXPOSURE_TIME_OFFSET)
    {
        return camera_set_exposure_time(value);
    }

    // Gain
    if (address == GENICAM_GAIN_OFFSET)
    {
        return camera_set_gain((int)value);
    }

    // Brightness
    if (address == GENICAM_BRIGHTNESS_OFFSET)
    {
        return camera_set_brightness((int32_t)value);
    }

    // Contrast
    if (address == GENICAM_CONTRAST_OFFSET)
    {
        return camera_set_contrast((int32_t)value);
    }

    // Saturation
    if (address == GENICAM_SATURATION_OFFSET)
    {
        return camera_set_saturation((int32_t)value);
    }

    // White balance mode
    if (address == GENICAM_WHITE_BALANCE_MODE_OFFSET)
    {
        return camera_set_white_balance_mode((int)value);
    }

    // Trigger mode
    if (address == GENICAM_TRIGGER_MODE_OFFSET)
    {
        return camera_set_trigger_mode((int)value);
    }

    // Discovery broadcast enable
    if (address == GENICAM_DISCOVERY_BROADCAST_ENABLE_OFFSET)
    {
        gvcp_enable_discovery_broadcast(value != 0);
        return ESP_OK;
    }

    // Discovery broadcast interval
    if (address == GENICAM_DISCOVERY_BROADCAST_INTERVAL_OFFSET)
    {
        gvcp_set_discovery_broadcast_interval(value);
        return ESP_OK;
    }

    // Standard GVCP registers
    if (address == GVCP_TL_PARAMS_LOCKED_OFFSET)
    {
        gvcp_set_tl_params_locked(value);
        return ESP_OK;
    }

    if (address == GVCP_GEVSCPS_PACKET_SIZE_OFFSET)
    {
        // Validate packet size: must be 576-9000 bytes and aligned to 128 bytes
        if (value >= 576 && value <= 9000 && (value % 128) == 0)
        {
            packet_size = value;
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Invalid packet size: %d (must be 576-9000 and 128-byte aligned)", value);
        return ESP_FAIL;
    }

    if (address == GVCP_GEVSCPD_PACKET_DELAY_OFFSET)
    {
        // Map GVCP ticks to microseconds - assuming 1:1 mapping for simplicity
        packet_delay_us = value;
        return ESP_OK;
    }

    if (address == GVCP_GEVSCDA_DEST_ADDRESS_OFFSET)
    {
        // Store destination IP address (in network byte order)
        gvcp_set_stream_dest_address(value);
        ESP_LOGI(TAG, "Stream destination address set to: 0x%08x", value);
        return ESP_OK;
    }

    // Default: not writable or invalid
    return ESP_FAIL;
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
    if (packet_size < 8)
    {
        ESP_LOGE(TAG, "Invalid read memory command size: %d", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    if (data == NULL)
    {
        ESP_LOGE(TAG, "NULL data pointer in read memory command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    uint32_t address = ntohl(*(uint32_t *)data);
    uint32_t size = ntohl(*(uint32_t *)(data + 4));

    ESP_LOGI(TAG, "Read memory: addr=0x%08x, size=%d", address, size);

    // Enhanced size validation
    if (size == 0)
    {
        ESP_LOGW(TAG, "Read memory command with zero size");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    // Allow larger reads for XML content at address 0x10000 and XML region
    uint32_t max_read_size;
    if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size)
    {
        max_read_size = 8192; // Large reads allowed for XML region
    }
    else
    {
        max_read_size = 512; // Standard reads for other regions
    }

    if (size > max_read_size)
    {
        ESP_LOGW(TAG, "Read size %d exceeds maximum %d for address 0x%08x, clamping",
                 size, max_read_size, address);
        size = max_read_size;
    }

    // Address alignment check for certain registers (4-byte aligned for 32-bit registers)
    bool is_register_access = (address >= GENICAM_ACQUISITION_START_OFFSET &&
                               address <= GENICAM_TRIGGER_MODE_OFFSET);
    if (is_register_access && (address % 4 != 0))
    {
        ESP_LOGW(TAG, "Unaligned register access at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }

    // Create read memory ACK response - use dynamic allocation for large XML reads
    uint8_t *response;
    uint8_t stack_response[sizeof(gvcp_header_t) + 4 + 512];
    bool use_heap = (size > 512);

    if (use_heap)
    {
        response = malloc(sizeof(gvcp_header_t) + 4 + size);
        if (response == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for read memory response", sizeof(gvcp_header_t) + 4 + size);
            gvcp_send_nack(header, GVCP_ERROR_BUSY, client_addr);
            return;
        }
    }
    else
    {
        response = stack_response;
    }

    // Initialize response header
    gvcp_header_t *ack_header = (gvcp_header_t *)response;
    gvcp_create_ack_header(ack_header, header, GVCP_ACK_READ_MEMORY, GVCP_BYTES_TO_WORDS(4 + size));

    // Copy address back
    write_register_value(&response[sizeof(gvcp_header_t)], address, 4);

    // Copy memory data
    uint8_t *data_ptr = &response[sizeof(gvcp_header_t) + 4];

    if (address >= XML_BASE_ADDRESS && address < XML_BASE_ADDRESS + genicam_xml_size)
    {
        // GenICam XML memory region
        uint32_t xml_offset = address - XML_BASE_ADDRESS;
        uint32_t xml_read_size = size;

        ESP_LOGI(TAG, "XML read request: addr=0x%08x, offset=%d, requested_size=%d, xml_size=%d",
                 address, xml_offset, size, genicam_xml_size);

        // Validate XML size
        if (genicam_xml_size == 0)
        {
            ESP_LOGE(TAG, "ERROR: genicam_xml_size is 0!");
            gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
            if (use_heap)
                free(response);
            return;
        }

        if (xml_offset + xml_read_size > genicam_xml_size)
        {
            xml_read_size = genicam_xml_size - xml_offset;
            ESP_LOGI(TAG, "XML read size clamped to %d bytes", xml_read_size);
        }

        if (xml_read_size > 0)
        {
            memcpy(data_ptr, &genicam_xml_data[xml_offset], xml_read_size);
            ESP_LOGI(TAG, "XML data copied: %d bytes from offset %d", xml_read_size, xml_offset);

            // Log first few bytes for debugging
            if (xml_read_size >= 16)
            {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, data_ptr, 16, ESP_LOG_INFO);
            }
        }
        else
        {
            ESP_LOGW(TAG, "XML read size is 0 after bounds checking");
        }

        // Fill remaining with zeros if needed
        if (xml_read_size < size)
        {
            memset(data_ptr + xml_read_size, 0, size - xml_read_size);
            ESP_LOGI(TAG, "Filled %d bytes with zeros after XML data", size - xml_read_size);
        }
    }
    else
    {
        if (!handle_read_memory_cmd_inline(address, size, data_ptr))
        {
            ESP_LOGW(TAG, "Unhandled memory read: 0x%08x (size=%d), filled with zeros", address, size);
            memset(data_ptr, 0, size);
        }
    }

    // Send response
    size_t response_size = sizeof(gvcp_header_t) + 4 + size;
    esp_err_t err = gvcp_sendto(response, response_size, client_addr);

    // Cleanup heap allocation if used
    if (use_heap)
    {
        free(response);
    }

    // Update client activity
    gvsp_update_client_activity();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error sending read memory ACK");
    }
    else
    {
        ESP_LOGI(TAG, "Sent read memory ACK: %d total bytes, payload=%d bytes (%d words), data_len=%d",
                 response_size, (4 + size), (4 + size) / 4, size);
    }
}

void handle_write_memory_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size < 4)
    {
        ESP_LOGE(TAG, "Invalid write memory command size: %d", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    if (data == NULL)
    {
        ESP_LOGE(TAG, "NULL data pointer in write memory command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    uint32_t address = ntohl(*(uint32_t *)data);
    uint32_t size = packet_size - 4;
    const uint8_t *write_data = data + 4;

    ESP_LOGI(TAG, "Write memory: addr=0x%08x, size=%d", address, size);

    // Enhanced write validation
    if (size == 0 || size > 512)
    {
        ESP_LOGW(TAG, "Invalid write size: %d bytes", size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    // Address alignment check for register access
    bool is_register_access = (address >= GENICAM_ACQUISITION_START_OFFSET &&
                               address <= GENICAM_JPEG_QUALITY_OFFSET);
    if (is_register_access && (address % 4 != 0))
    {
        ESP_LOGW(TAG, "Unaligned register write at 0x%08x", address);
        gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
        return;
    }

    // Handle different write regions
    if (size == 4 && (address % 4 == 0) &&
        is_register_address_valid(address) &&
        is_register_address_writable(address))
    {

        uint32_t value = ntohl(*(uint32_t *)write_data);
        if (handle_write_memory_cmd_inline(address, value) == ESP_OK)
        {
            // Success ACK
            uint8_t response[sizeof(gvcp_header_t) + 4];

            gvcp_header_t *ack_header = (gvcp_header_t *)response;
            gvcp_create_ack_header(ack_header, header, GVCP_ACK_WRITE_MEMORY, GVCP_BYTES_TO_WORDS(4));

            *(uint32_t *)&response[sizeof(gvcp_header_t)] = htonl(address);

            esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);
            gvsp_update_client_activity();

            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Error sending write memory ACK");
            }
            else
            {
                ESP_LOGI(TAG, "Sent write memory ACK");
            }
            return;
        }
        else
        {
            ESP_LOGW(TAG, "Register write to 0x%08x failed", address);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    }

    if (address >= GVBS_USER_DEFINED_NAME_OFFSET && address + size <= GVBS_USER_DEFINED_NAME_OFFSET + 16)
    {
        uint8_t *bootstrap_memory = get_bootstrap_memory();
        memcpy(&bootstrap_memory[address], write_data, size);

        uint8_t response[sizeof(gvcp_header_t) + 4];

        gvcp_header_t *ack_header = (gvcp_header_t *)response;
        gvcp_create_ack_header(ack_header, header, GVCP_ACK_WRITE_MEMORY, GVCP_BYTES_TO_WORDS(4));

        *(uint32_t *)&response[sizeof(gvcp_header_t)] = htonl(address);

        esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);
        gvsp_update_client_activity();

        ESP_LOGI(TAG, "Wrote %d bytes to bootstrap name region", size);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error sending write memory ACK");
        }
        else
        {
            ESP_LOGI(TAG, "Sent write memory ACK");
        }
        return;
    }

    // Reject unhandled memory regions
    ESP_LOGW(TAG, "Unhandled memory write: addr=0x%08x, size=%d", address, size);
    gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
}

void handle_readreg_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    uint16_t packet_size = ntohs(header->size);
    uint16_t payload_bytes = packet_size * 4;

    if (data == NULL || payload_bytes == 0 || payload_bytes % 4 != 0)
    {
        ESP_LOGE(TAG, "Invalid READREG packet: size=%d bytes", payload_bytes);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    int num_registers = payload_bytes / 4;
    ESP_LOGI(TAG, "READREG request: %d registers", num_registers);

    // Validate all addresses first
    for (int i = 0; i < num_registers; i++)
    {
        uint32_t address = ntohl(((uint32_t *)data)[i]);

        if (address % 4 != 0)
        {
            ESP_LOGW(TAG, "Unaligned address: 0x%08x", address);
            gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
            return;
        }

        if (!is_register_address_valid(address))
        {
            ESP_LOGW(TAG, "Invalid register address: 0x%08x", address);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
            return;
        }
    }

    // Allocate response buffer
    size_t response_size = sizeof(gvcp_header_t) + 4 * num_registers;
    uint8_t *response = malloc(response_size);
    if (!response)
    {
        ESP_LOGE(TAG, "Out of memory for READREG response (%zu bytes)", response_size);
        gvcp_send_nack(header, GVCP_ERROR_BUSY, client_addr);
        return;
    }

    gvcp_header_t *ack_header = (gvcp_header_t *)response;
    gvcp_create_ack_header(ack_header, header, GVCP_ACK_READREG, num_registers);

    uint8_t *payload = response + sizeof(gvcp_header_t);

    // Fill response payload with register values
    for (int i = 0; i < num_registers; i++)
    {
        uint32_t address = ntohl(((uint32_t *)data)[i]);

        // Use internal logic to read register values (already aligned and valid)
        uint8_t temp[4] = {0};
        write_register_value(temp, 0, 4); // fallback default

        // Reuse core read logic for individual registers
        uint8_t read_data[8];
        write_register_value(&read_data[0], address, 4);
        write_register_value(&read_data[4], 4, 4);

        // You may refactor out the address-based value resolution into a shared function.
        handle_read_memory_cmd_inline(address, 4, temp);

        memcpy(&payload[i * 4], temp, 4); // Copy 4-byte value
    }

    // Send the response
    esp_err_t err = gvcp_sendto(response, response_size, client_addr);
    free(response);
    gvsp_update_client_activity();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send READREG ACK");
    }
    else
    {
        ESP_LOGI(TAG, "Sent READREG ACK with %d registers", num_registers);
    }
}

void handle_writereg_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    uint16_t packet_size = ntohs(header->size);
    uint16_t payload_bytes = packet_size * 4;

    if (data == NULL || payload_bytes == 0 || payload_bytes % 8 != 0)
    {
        ESP_LOGE(TAG, "Invalid WRITEREG packet: size=%d bytes", payload_bytes);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    int num_registers = payload_bytes / 8;
    ESP_LOGI(TAG, "WRITEREG request: %d address-value pairs", num_registers);

    // Validate all addresses first
    for (int i = 0; i < num_registers; i++)
    {
        uint32_t address = ntohl(*(uint32_t *)&data[i * 8]);
        if (address % 4 != 0)
        {
            ESP_LOGW(TAG, "Unaligned register write: 0x%08x", address);
            gvcp_send_nack(header, GVCP_ERROR_BAD_ALIGNMENT, client_addr);
            return;
        }

        if (!is_register_address_valid(address))
        {
            ESP_LOGW(TAG, "Invalid register address: 0x%08x", address);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_ADDRESS, client_addr);
            return;
        }

        if (!is_register_address_writable(address))
        {
            ESP_LOGW(TAG, "Read-only register address: 0x%08x", address);
            gvcp_send_nack(header, GVCP_ERROR_ACCESS_DENIED, client_addr);
            return;
        }
    }

    // Perform writes
    for (int i = 0; i < num_registers; i++)
    {
        uint32_t address = ntohl(*(uint32_t *)&data[i * 8]);
        uint32_t value = ntohl(*(uint32_t *)&data[i * 8 + 4]);

        esp_err_t err = handle_write_memory_cmd_inline(address, value);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Write to 0x%08x failed", address);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }
    }

    // Create response with address list
    size_t response_size = sizeof(gvcp_header_t) + 4 * num_registers;
    uint8_t *response = malloc(response_size);
    if (!response)
    {
        ESP_LOGE(TAG, "Out of memory for WRITEREG ACK");
        gvcp_send_nack(header, GVCP_ERROR_BUSY, client_addr);
        return;
    }

    gvcp_header_t *ack_header = (gvcp_header_t *)response;
    gvcp_create_ack_header(ack_header, header, GVCP_ACK_WRITEREG, num_registers);

    // Copy addresses into response payload
    uint8_t *payload = response + sizeof(gvcp_header_t);
    for (int i = 0; i < num_registers; i++)
    {
        uint32_t address = ntohl(*(uint32_t *)&data[i * 8]);
        *(uint32_t *)&payload[i * 4] = htonl(address);
    }

    esp_err_t err = gvcp_sendto(response, response_size, client_addr);
    free(response);
    gvsp_update_client_activity();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send WRITEREG ACK");
    }
    else
    {
        ESP_LOGI(TAG, "Sent WRITEREG ACK with %d registers", num_registers);
    }
}

void handle_packetresend_cmd(const gvcp_header_t *header, const uint8_t *data, struct sockaddr_in *client_addr)
{
    // Enhanced validation
    uint16_t packet_size = ntohs(header->size);
    if (packet_size < 8)
    {
        ESP_LOGE(TAG, "Invalid packet resend command size: %d (minimum 8)", packet_size);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    if (data == NULL)
    {
        ESP_LOGE(TAG, "NULL data pointer in packet resend command");
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    // Extract resend parameters - standard GVCP packet resend format
    uint32_t stream_channel_index = ntohl(*(uint32_t *)data);
    uint32_t block_id = ntohl(*(uint32_t *)(data + 4));

    ESP_LOGI(TAG, "Packet resend request: stream=%d, block_id=%d", stream_channel_index, block_id);

    // Validate stream channel (we only support channel 0)
    if (stream_channel_index != 0)
    {
        ESP_LOGW(TAG, "Invalid stream channel index: %d", stream_channel_index);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    // Check if streaming is active
    if (!gvsp_is_streaming())
    {
        ESP_LOGW(TAG, "Packet resend requested but streaming is not active");
        gvcp_send_nack(header, GVCP_ERROR_WRONG_CONFIG, client_addr);
        return;
    }

    // Attempt to resend the requested frame/block
    esp_err_t resend_result = gvsp_resend_frame(block_id);

    if (resend_result != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to resend block_id %d: %s", block_id, esp_err_to_name(resend_result));
        // Send NACK for unavailable data
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    // Create packet resend ACK response
    uint8_t response[sizeof(gvcp_header_t) + 8];
    gvcp_header_t *ack_header = (gvcp_header_t *)response;

    // Create ACK response header
    gvcp_create_ack_header(ack_header, header, GVCP_ACK_PACKETRESEND, GVCP_BYTES_TO_WORDS(8));

    // Echo back the resend parameters
    write_register_value(&response[sizeof(gvcp_header_t)], stream_channel_index, 4);
    write_register_value(&response[sizeof(gvcp_header_t) + 4], block_id, 4);

    // Send response
    esp_err_t err = gvcp_sendto(response, sizeof(response), client_addr);

    // Update client activity
    gvsp_update_client_activity();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error sending packet resend ACK");
    }
    else
    {
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

    // Initialize standard GVCP registers
    tl_params_locked = 0;
    stream_dest_address = 0;

    ESP_LOGI(TAG, "Register access module initialized with standard GVCP registers");
    return ESP_OK;
}