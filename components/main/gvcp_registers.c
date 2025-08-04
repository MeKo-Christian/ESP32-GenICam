#include "gvcp_registers.h"
#include "gvcp_protocol.h"
#include "gvcp_bootstrap.h"
#include "gvcp_statistics.h"
#include "gvcp_discovery.h"
#include "gvcp_handler.h"
#include "camera_handler.h"
#include "gvsp_handler.h"
#include "genicam_xml.h"
#include "status_led.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <sys/param.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

static const char *TAG = "gvcp_registers";

// Helper function to convert uint32_t to float
static inline float gvcp_u32_to_float(uint32_t raw_value)
{
    float result;
    memcpy(&result, &raw_value, sizeof(result));
    return result;
}

// Helper function to convert float to uint32_t
static inline uint32_t gvcp_float_to_u32(float value)
{
    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    return raw;
}

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

// External declaration of sendto function
extern esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);

// Stream control parameters
static uint32_t packet_delay_us = 1000; // Inter-packet delay in microseconds (default 1ms)
static float frame_rate_fps = 1.0f;     // Frame rate in FPS (default 1 FPS)
static uint32_t packet_size = 1400;     // Data packet size (default 1400)
static uint32_t stream_status = 0;      // Stream status register

// Acquisition control state
static uint32_t acquisition_mode = 0; // 0 = Continuous
static uint32_t acquisition_start_reg = 0;
static uint32_t acquisition_stop_reg = 0;

// Standard GVCP registers for Aravis compatibility
static uint32_t tl_params_locked = 0;    // 0x0A00 - TLParamsLocked
static uint32_t stream_dest_address = 0; // 0x0A10 - GevSCDA (destination IP)

// Stream Channel Configuration (SCCFG) registers - GigE Vision 2.0+
static uint32_t multipart_config = 0; // 0x0D24 - SCCFG multipart register (bit 0: multipart enable)
static uint32_t sccfg_register = 0;   // 0x0D20 - GevSCCfg main configuration register

// Stream channel and network interface registers
static uint32_t stream_channel_count = 1;   // 0x0D00 - Number of stream channels (always 1)
static uint32_t num_network_interfaces = 1; // 0x0600 - Number of network interfaces (always 1)
static uint32_t scphost_port = 0;           // 0x0D10 - Stream channel host port
static uint32_t scps_packet_size = 1400;    // 0x0D04 - Stream channel packet size

// Additional Aravis-specific SCCFG registers
static uint32_t aravis_multipart_reg = 0; // 0x0D30 - ArvGevSCCFGMultipartReg
static uint32_t aravis_multipart_cap = 0; // 0x0D34 - ArvGevSCCAPMultipartReg

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

    // GigE Vision Timestamp registers
    if (address == GVCP_GEV_TIMESTAMP_CONTROL_LATCH_OFFSET ||
        address == GVCP_GEV_TIMESTAMP_VALUE_HIGH_OFFSET)
    {
        return true;
    }

    // Stream Channel Configuration (SCCFG) registers (0x0D00-0x0D24)
    if (address == GVCP_GEVSC_CFG_MULTIPART_OFFSET ||
        address == GVCP_GEV_N_STREAM_CHANNELS_OFFSET ||
        address == GVCP_GEV_N_NETWORK_INTERFACES_OFFSET ||
        address == GVCP_GEV_SCP_HOST_PORT_OFFSET ||
        address == GVCP_GEV_SCPS_PACKET_SIZE_OFFSET ||
        address == GVCP_GEV_SCDA_DEST_ADDRESS_OFFSET ||
        address == GVCP_GEVSCCFG_REGISTER_OFFSET ||
        address == GVCP_GEVSC_CFG_MULTIPART_OFFSET ||
        address == GVCP_GEVSC_CFG_ARAVIS_MULTIPART_OFFSET ||
        address == GVCP_GEVSC_CFG_CAP_MULTIPART_OFFSET)
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
        address == GENICAM_PACKET_SIZE_OFFSET ||
        address == GENICAM_PACKET_DELAY_OFFSET ||
        address == GVCP_GEVSCDA_DEST_ADDRESS_OFFSET)
    {
        return true;
    }

    // Stream Channel Configuration (SCCFG) registers - writable (multipart enable)
    if (address == GVCP_GEVSC_CFG_MULTIPART_OFFSET ||
        address == GVCP_GEV_SCP_HOST_PORT_OFFSET ||
        address == GVCP_GEV_SCPS_PACKET_SIZE_OFFSET ||
        address == GVCP_GEV_SCDA_DEST_ADDRESS_OFFSET)
    {
        return true;
    }

    // Read-only stream channel registers
    if (address == GVCP_GEV_N_STREAM_CHANNELS_OFFSET ||
        address == GVCP_GEV_N_NETWORK_INTERFACES_OFFSET)
    {
        return false;
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

float gvcp_get_frame_rate_fps(void)
{
    return frame_rate_fps;
}

uint32_t gvcp_get_packet_size(void)
{
    return packet_size;
}

uint32_t gvcp_get_scphost_port(void)
{
    return scphost_port;
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
    ESP_LOGI(TAG, "🔍 READ_REG: addr=0x%08x, size=%d", address, size);

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
        else if (address == GVCP_GEV_N_NETWORK_INTERFACES_OFFSET)
        {
            ESP_LOGI(TAG, "Bootstrap: Reading GevNumberOfNetworkInterfaces (0x%08x): returning %d", address, num_network_interfaces);
            write_register_value(out, num_network_interfaces, size);
        }
        else if (address == GVCP_GEV_N_STREAM_CHANNELS_OFFSET)
        {
            ESP_LOGI(TAG, "Bootstrap: Reading GevStreamChannelCount (0x%08x): returning %d", address, stream_channel_count);
            write_register_value(out, stream_channel_count, size);
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
        uint32_t encoded = gvcp_float_to_u32(frame_rate_fps);
        write_register_value(out, encoded, size);
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
        // ExposureTime is defined as FloatReg, so convert uint32_t microseconds to float
        float exposure_float = (float)camera_get_exposure_time(); // Keep in microseconds
        uint32_t encoded = gvcp_float_to_u32(exposure_float);
        write_register_value(out, encoded, size);
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
    else if (address == GENICAM_PACKET_SIZE_OFFSET)
    {
        write_register_value(out, packet_size, size);
    }
    else if (address == GENICAM_PACKET_DELAY_OFFSET)
    {
        write_register_value(out, packet_delay_us, size);
    }
    else if (address == GVCP_GEVSCDA_DEST_ADDRESS_OFFSET)
    {
        write_register_value(out, gvcp_get_stream_dest_address(), size);
    }
    else if (address == GVCP_GEV_SCDA_DEST_ADDRESS_OFFSET)
    {
        write_register_value(out, gvcp_get_stream_dest_address(), size);
    }
    // Stream Channel Configuration (SCCFG) registers
    else if (address == GVCP_GEVSCCFG_REGISTER_OFFSET)
    {
        PROTOCOL_LOG_I(TAG, "Reading GevSCCfg (0x%08x): returning 0x%08x", address, sccfg_register);
        write_register_value(out, sccfg_register, size);
    }
    else if (address == GVCP_GEVSC_CFG_MULTIPART_OFFSET)
    {
        write_register_value(out, multipart_config, size);
    }
    else if (address == GVCP_GEV_N_STREAM_CHANNELS_OFFSET)
    {
        PROTOCOL_LOG_I(TAG, "Reading GevStreamChannelCount (0x%08x): returning %d", address, stream_channel_count);
        write_register_value(out, stream_channel_count, size);
    }
    else if (address == GVCP_GEV_N_NETWORK_INTERFACES_OFFSET)
    {
        PROTOCOL_LOG_I(TAG, "Reading GevNumberOfNetworkInterfaces (0x%08x): returning %d", address, num_network_interfaces);
        write_register_value(out, num_network_interfaces, size);
    }
    else if (address == GVCP_GEV_SCP_HOST_PORT_OFFSET)
    {
        write_register_value(out, scphost_port, size);
    }
    else if (address == GVCP_GEV_SCPS_PACKET_SIZE_OFFSET)
    {
        write_register_value(out, scps_packet_size, size);
    }
    else if (address == GVCP_GEV_TIMESTAMP_TICK_FREQ_HIGH_OFFSET)
    {
        // Return 1 MHz (1000000 Hz) tick frequency for ESP32 microsecond timer resolution
        uint32_t tick_frequency = 1000000;
        PROTOCOL_LOG_I(TAG, "Reading GevTimestampTickFrequency (0x%08x): returning %d Hz", address, tick_frequency);
        write_register_value(out, tick_frequency, size);
    }
    else if (address == GVCP_GEVSC_CFG_ARAVIS_MULTIPART_OFFSET)
    {
        PROTOCOL_LOG_I(TAG, "Reading ArvGevSCCFGMultipartReg (0x%08x): returning 0x%08x", address, aravis_multipart_reg);
        write_register_value(out, aravis_multipart_reg, size);
    }
    else if (address == GVCP_GEVSC_CFG_CAP_MULTIPART_OFFSET)
    {
        PROTOCOL_LOG_I(TAG, "Reading ArvGevSCCAPMultipartReg (0x%08x): returning 0x%08x", address, aravis_multipart_cap);
        write_register_value(out, aravis_multipart_cap, size);
    }
    else if (address == GVCP_GEV_TIMESTAMP_CONTROL_LATCH_OFFSET)
    {
        // GevTimestampControlLatch: return 0 (no latching behavior implemented)
        uint32_t timestamp_control = 0;
        PROTOCOL_LOG_I(TAG, "Reading GevTimestampControlLatch (0x%08x): returning %d", address, timestamp_control);
        write_register_value(out, timestamp_control, size);
    }
    else if (address == GVCP_GEV_TIMESTAMP_VALUE_HIGH_OFFSET)
    {
        // GevTimestampValue: return current timestamp in microseconds (aligned with tick frequency)
        uint64_t timestamp_us = esp_timer_get_time();
        uint32_t timestamp_value = (uint32_t)(timestamp_us & 0xFFFFFFFF); // Use lower 32 bits
        PROTOCOL_LOG_I(TAG, "Reading GevTimestampValue (0x%08x): returning %u us", address, timestamp_value);
        write_register_value(out, timestamp_value, size);
    }
    else
    {
        ESP_LOGW(TAG, "🔍 READ_REG: UNKNOWN addr=0x%08x - returning zeros", address);
        memset(out, 0, size);
    }

    // Return success for all handled register addresses above
    return true;

    // Log the returned value for debugging
    if (size >= 4)
    {
        uint32_t returned_value = ntohl(*(uint32_t *)out);
        ESP_LOGI(TAG, "🔍 READ_REG: addr=0x%08x -> value=0x%08x", address, returned_value);
    }

    return true;
}

static esp_err_t handle_write_memory_cmd_inline(uint32_t address, uint32_t value)
{
    ESP_LOGI(TAG, "✍️ WRITE_REG: addr=0x%08x, value=0x%08x", address, value);
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

    // Frame rate (FloatReg: 0x00001014)
    if (address == GENICAM_FRAME_RATE_OFFSET)
    {
        float fps = gvcp_u32_to_float(value);
        if (fps >= 1.0f && fps <= 30.0f)
        {
            ESP_LOGI(TAG, "Set frame_rate_fps to %.2f", fps);
            frame_rate_fps = fps;
            return ESP_OK;
        }
        else
        {
            ESP_LOGW(TAG, "Invalid frame_rate_fps: %.2f (must be between 1 and 30)", fps);
            return ESP_ERR_INVALID_ARG;
        }
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
        // ExposureTime is defined as FloatReg, so convert float to uint32_t microseconds
        float exposure_float = gvcp_u32_to_float(value);
        if (exposure_float >= 100.0f && exposure_float <= 1000000.0f) // 100us to 1s range
        {
            ESP_LOGI(TAG, "Set exposure_time to %.1f us", exposure_float);
            return camera_set_exposure_time((uint32_t)exposure_float);
        }
        else
        {
            ESP_LOGW(TAG, "Invalid exposure time: %.1f us (must be between 100-1000000)", exposure_float);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Gain
    if (address == GENICAM_GAIN_OFFSET)
    {
        if (value >= 0 && value <= 30) // Valid gain range 0-30 dB
        {
            ESP_LOGI(TAG, "Set gain to %d dB", value);
            return camera_set_gain((int)value);
        }
        else
        {
            ESP_LOGW(TAG, "Invalid gain: %d dB (must be between 0-30)", value);
            return ESP_ERR_INVALID_ARG;
        }
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

    if (address == GENICAM_PACKET_SIZE_OFFSET)
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

    if (address == GENICAM_PACKET_DELAY_OFFSET)
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

    if (address == GVCP_GEV_SCDA_DEST_ADDRESS_OFFSET)
    {
        // Store destination IP address (in network byte order) - duplicate of 0x0A10
        gvcp_set_stream_dest_address(value);
        ESP_LOGI(TAG, "Stream destination address (duplicate) set to: 0x%08x", value);
        return ESP_OK;
    }

    // Stream Channel Configuration (SCCFG) registers
    if (address == GVCP_GEVSC_CFG_MULTIPART_OFFSET)
    {
        // Store multipart configuration (bit 0: enable/disable multipart)
        multipart_config = value;
        ESP_LOGI(TAG, "Multipart configuration set to: 0x%08x (multipart %s)",
                 value, (value & 0x1) ? "enabled" : "disabled");
        return ESP_OK;
    }

    if (address == GVCP_GEV_SCP_HOST_PORT_OFFSET)
    {
        // Store stream channel host port
        scphost_port = value;
        ESP_LOGI(TAG, "Stream channel host port set to: %d", value);
        return ESP_OK;
    }

    if (address == GVCP_GEV_SCPS_PACKET_SIZE_OFFSET)
    {
        // Validate packet size: must be 576-9000 bytes
        if (value >= 576 && value <= 9000)
        {
            scps_packet_size = value;
            PROTOCOL_LOG_I(TAG, "Stream channel packet size set to: %d", value);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Invalid stream channel packet size: %d (must be 576-9000)", value);
        return ESP_FAIL;
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
    uint16_t packet_words = ntohs(header->size);
    uint16_t packet_bytes = packet_words * 4;

    if (packet_bytes < 8)
    {
        ESP_LOGE(TAG, "Invalid read memory command size: %d", packet_bytes);
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
                PROTOCOL_LOG_BUFFER_HEX(TAG, data_ptr, 16, ESP_LOG_INFO);
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

void handle_readreg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr)
{
    uint16_t header_size_words = ntohs(header->size);
    uint16_t header_payload_bytes = header_size_words * 4;

    // Enhanced debug logging to understand size field interpretation
    ESP_LOGI(TAG, "READREG debug: header->size=%d (0x%04x), interpreted as %d bytes, actual_data_len=%d",
             header_size_words, header_size_words, header_payload_bytes, data_len);
    ESP_LOGI(TAG, "READREG analysis: if_words=%d bytes, if_bytes=%d bytes, received=%d bytes",
             header_size_words * 4, header_size_words, data_len);

    // Hex dump of the complete packet for analysis
    PROTOCOL_LOG_I(TAG, "READREG packet hex dump (header + payload):");
    PROTOCOL_LOG_BUFFER_HEX(TAG, header, sizeof(gvcp_header_t), ESP_LOG_INFO);
    if (data && data_len > 0)
    {
        PROTOCOL_LOG_I(TAG, "READREG payload hex dump (%d bytes):", data_len);
        PROTOCOL_LOG_BUFFER_HEX(TAG, data, MIN(data_len, 64), ESP_LOG_INFO);
    }

    // Log header contents
    ESP_LOGI(TAG, "READREG header: type=0x%02x, flags=0x%02x, cmd=0x%04x, size=%d, id=0x%04x",
             header->packet_type, header->packet_flags, ntohs(header->command),
             ntohs(header->size), ntohs(header->id));

    // Use actual received data length instead of header size field
    if (data == NULL || data_len == 0 || data_len % 4 != 0)
    {
        ESP_LOGE(TAG, "Invalid READREG packet: data_len=%d bytes (must be multiple of 4)", data_len);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    // Cross-validate header size field with actual data length
    if (header_payload_bytes != data_len)
    {
        ESP_LOGW(TAG, "READREG size mismatch: header claims %d bytes, received %d bytes",
                 header_payload_bytes, data_len);
    }
    else
    {
        ESP_LOGI(TAG, "READREG size validation: header and actual data length match (%d bytes)", data_len);
    }

    int num_registers = data_len / 4;
    ESP_LOGI(TAG, "READREG request: %d registers", num_registers);

    // Hex dump of payload data for debugging
    if (data_len > 0)
    {
        PROTOCOL_LOG_I(TAG, "READREG payload hex dump (%d bytes):", data_len);
        PROTOCOL_LOG_BUFFER_HEX(TAG, data, MIN(data_len, 64), ESP_LOG_INFO);
    }

    // Validate all addresses first
    for (int i = 0; i < num_registers; i++)
    {
        // Add boundary check before reading
        if ((i * 4 + 3) >= data_len)
        {
            ESP_LOGE(TAG, "READREG: Address read beyond payload boundary at index %d (offset %d >= %d)",
                     i, i * 4 + 3, data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }

        uint32_t address = ntohl(((uint32_t *)data)[i]);

        ESP_LOGI(TAG, "READREG[%d]: parsing offset %d, raw_addr=0x%08x, addr=0x%08x",
                 i, i * 4, ((uint32_t *)data)[i], address);

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
    // Pass payload size in words (using GVCP_BYTES_TO_WORDS macro for consistency)
    gvcp_create_ack_header(ack_header, header, GVCP_ACK_READREG, GVCP_BYTES_TO_WORDS(num_registers * 4));

    uint8_t *payload = response + sizeof(gvcp_header_t);

    // Fill response payload with register values
    for (int i = 0; i < num_registers; i++)
    {
        uint32_t address = ntohl(((uint32_t *)data)[i]);

        // Read register value using internal logic
        uint8_t register_value[4] = {0};
        handle_read_memory_cmd_inline(address, 4, register_value);

        // Copy the 4-byte register value to response payload
        memcpy(&payload[i * 4], register_value, 4);

        ESP_LOGI(TAG, "READREG[%d]: addr=0x%08x, value=0x%08x",
                 i, address, ntohl(*(uint32_t *)register_value));
    }

    // Log the response packet details before sending
    PROTOCOL_LOG_I(TAG, "READREG ACK packet: type=0x%02x, cmd=0x%04x, size=%d words, %d registers",
                   ack_header->packet_type, ntohs(ack_header->command), ntohs(ack_header->size), num_registers);
    PROTOCOL_LOG_I(TAG, "READREG response buffer: allocated=%zu bytes, header=%zu bytes, payload=%d bytes",
                   response_size, sizeof(gvcp_header_t), num_registers * 4);

    // Hex dump the complete response for analysis
    PROTOCOL_LOG_I(TAG, "READREG complete response hex dump (%zu bytes):", response_size);
    PROTOCOL_LOG_BUFFER_HEX(TAG, response, response_size, ESP_LOG_INFO);

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
        PROTOCOL_LOG_I(TAG, "Successfully sent READREG ACK with %d registers", num_registers);
    }
}

void handle_writereg_cmd(const gvcp_header_t *header, const uint8_t *data, int data_len, struct sockaddr_in *client_addr)
{
    uint16_t header_size = ntohs(header->size);
    uint16_t header_payload_bytes = header_size * 4;

    // Enhanced debug logging to understand size field interpretation
    ESP_LOGI(TAG, "WRITEREG debug: header->size=%d (0x%04x), header_payload_bytes=%d, actual_data_len=%d",
             header_size, header_size, header_payload_bytes, data_len);
    ESP_LOGI(TAG, "WRITEREG analysis: if_words=%d bytes, if_bytes=%d bytes, received=%d bytes",
             header_size * 4, header_size, data_len);

    // Hex dump of the complete packet for analysis
    PROTOCOL_LOG_I(TAG, "WRITEREG packet hex dump (header + payload):");
    PROTOCOL_LOG_BUFFER_HEX(TAG, header, sizeof(gvcp_header_t), ESP_LOG_INFO);
    if (data && data_len > 0)
    {
        PROTOCOL_LOG_I(TAG, "WRITEREG payload hex dump (%d bytes):", data_len);
        PROTOCOL_LOG_BUFFER_HEX(TAG, data, MIN(data_len, 64), ESP_LOG_INFO);
    }

    // Log header contents
    ESP_LOGI(TAG, "WRITEREG header: type=0x%02x, flags=0x%02x, cmd=0x%04x, size=%d, id=0x%04x",
             header->packet_type, header->packet_flags, ntohs(header->command),
             ntohs(header->size), ntohs(header->id));

    // Use actual received data length instead of header size field
    if (data == NULL || data_len == 0 || data_len % 8 != 0)
    {
        ESP_LOGE(TAG, "Invalid WRITEREG packet: data_len=%d bytes (must be multiple of 8)", data_len);
        gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
        return;
    }

    // Cross-validate header size field with actual data length
    if (header_payload_bytes != data_len)
    {
        ESP_LOGW(TAG, "WRITEREG size mismatch: header claims %d bytes, received %d bytes",
                 header_payload_bytes, data_len);
    }
    else
    {
        ESP_LOGI(TAG, "WRITEREG size validation: header and actual data length match (%d bytes)", data_len);
    }

    int num_registers = data_len / 8;
    PROTOCOL_LOG_I(TAG, "WRITEREG request: %d address-value pairs", num_registers);

    // Hex dump of payload data for debugging
    if (data_len > 0)
    {
        PROTOCOL_LOG_I(TAG, "WRITEREG payload hex dump (%d bytes):", data_len);
        PROTOCOL_LOG_BUFFER_HEX(TAG, data, MIN(data_len, 64), ESP_LOG_INFO);
    }

    // Validate all addresses first
    for (int i = 0; i < num_registers; i++)
    {
        // Add boundary check before reading
        if ((i * 8 + 7) >= data_len)
        {
            ESP_LOGE(TAG, "WRITEREG: Address-value pair read beyond payload boundary at index %d (offset %d >= %d)",
                     i, i * 8 + 7, data_len);
            gvcp_send_nack(header, GVCP_ERROR_INVALID_PARAMETER, client_addr);
            return;
        }

        uint32_t address = ntohl(*(uint32_t *)&data[i * 8]);
        uint32_t value = ntohl(*(uint32_t *)&data[i * 8 + 4]);

        ESP_LOGI(TAG, "WRITEREG[%d]: parsing offset %d, raw_addr=0x%08x, addr=0x%08x, value=0x%08x",
                 i, i * 8, *(uint32_t *)&data[i * 8], address, value);

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
    // Pass payload size in words (using GVCP_BYTES_TO_WORDS macro for consistency)
    gvcp_create_ack_header(ack_header, header, GVCP_ACK_WRITEREG, GVCP_BYTES_TO_WORDS(num_registers * 4));

    // Copy addresses into response payload
    uint8_t *payload = response + sizeof(gvcp_header_t);
    for (int i = 0; i < num_registers; i++)
    {
        uint32_t address = ntohl(*(uint32_t *)&data[i * 8]);
        *(uint32_t *)&payload[i * 4] = htonl(address);
    }

    // Log the response packet details before sending
    PROTOCOL_LOG_I(TAG, "WRITEREG ACK packet: type=0x%02x, cmd=0x%04x, size=%d words, %d registers",
                   ack_header->packet_type, ntohs(ack_header->command), ntohs(ack_header->size), num_registers);

    esp_err_t err = gvcp_sendto(response, response_size, client_addr);
    free(response);
    gvsp_update_client_activity();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send WRITEREG ACK");
    }
    else
    {
        PROTOCOL_LOG_I(TAG, "Successfully sent WRITEREG ACK with %d registers", num_registers);
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

    PROTOCOL_LOG_I(TAG, "Packet resend request: stream=%d, block_id=%d", stream_channel_index, block_id);

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
        PROTOCOL_LOG_I(TAG, "Sent packet resend ACK for block_id %d", block_id);
    }
}

esp_err_t gvcp_registers_init(void)
{
    // Initialize stream parameters
    packet_delay_us = 1000;
    frame_rate_fps = 15.0;
    packet_size = 1400;
    stream_status = 0;

    // Initialize acquisition state
    acquisition_mode = 0;
    acquisition_start_reg = 0;
    acquisition_stop_reg = 0;

    // Initialize standard GVCP registers
    tl_params_locked = 0;
    stream_dest_address = 0;

    // Initialize SCCFG registers
    multipart_config = 0; // Multipart disabled by default
    sccfg_register = 0;   // Main stream channel configuration register

    // Initialize stream channel and network interface registers
    stream_channel_count = 1;   // This device supports 1 stream channel
    num_network_interfaces = 1; // This device has 1 network interface (WiFi)
    scphost_port = 0;           // Default host port (will be set by client)
    scps_packet_size = 1400;    // Default packet size

    // Initialize Aravis-specific SCCFG registers
    aravis_multipart_reg = 0; // Aravis multipart configuration
    aravis_multipart_cap = 0; // Aravis multipart capabilities

    PROTOCOL_LOG_I(TAG, "Register access module initialized with standard GVCP registers and SCCFG support");
    ESP_LOGI(TAG, "Stream channels: %d, Network interfaces: %d", stream_channel_count, num_network_interfaces);
    return ESP_OK;
}

// Multipart payload control API functions
bool gvcp_get_multipart_enabled(void)
{
    return (multipart_config & 0x1) != 0;
}

void gvcp_set_multipart_enabled(bool enabled)
{
    if (enabled)
    {
        multipart_config |= 0x1; // Set bit 0
    }
    else
    {
        multipart_config &= ~0x1; // Clear bit 0
    }
    ESP_LOGI(TAG, "Multipart payload %s", enabled ? "enabled" : "disabled");
}

uint32_t gvcp_get_multipart_config(void)
{
    return multipart_config;
}

void gvcp_set_multipart_config(uint32_t config)
{
    multipart_config = config;
    ESP_LOGI(TAG, "Multipart configuration set to: 0x%08x", config);
}