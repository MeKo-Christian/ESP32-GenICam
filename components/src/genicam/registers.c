#include "registers.h"
#include "../utils/platform.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "genicam_registers";

// Register storage for GenICam registers (non-bootstrap)
#define GENICAM_REGISTER_COUNT 64
static uint32_t genicam_register_values[GENICAM_REGISTER_COUNT];

// Bootstrap memory callback
static genicam_registers_get_bootstrap_callback_t get_bootstrap_callback = NULL;

// Configuration registers
static uint32_t tl_params_locked = 0;
static uint32_t stream_dest_address = 0;
static bool multipart_enabled = false;
static uint32_t multipart_config = 0;

// Camera parameters
static uint32_t exposure_time_us = 10000; // 10ms default
static uint32_t gain = 100; // 1.0x gain (100 = 1.0)
static uint32_t pixel_format = 0x01080001; // Mono8 default

// Statistics
static uint32_t total_commands = 0;
static uint32_t total_errors = 0;
static uint32_t unknown_commands = 0;
static uint32_t connection_status = 0;

void genicam_registers_set_bootstrap_callback(genicam_registers_get_bootstrap_callback_t callback) {
    get_bootstrap_callback = callback;
}

// Helper function to get register index from address
static int get_register_index(uint32_t address) {
    if (address >= 0x00001000 && address < 0x00001000 + (GENICAM_REGISTER_COUNT * 4)) {
        return (address - 0x00001000) / 4;
    }
    return -1;
}

bool genicam_registers_is_address_valid(uint32_t address) {
    // Bootstrap registers
    if (genicam_registers_is_bootstrap_register(address)) {
        return true;
    }
    
    // GenICam registers
    if (genicam_registers_is_genicam_register(address)) {
        return true;
    }
    
    // Standard GVCP registers
    if ((address >= 0x00000600 && address <= 0x00000D50) ||
        (address >= 0x00000904 && address <= 0x0000094C)) {
        return true;
    }
    
    return false;
}

bool genicam_registers_is_address_writable(uint32_t address) {
    // Bootstrap registers are generally read-only except for some specific ones
    if (genicam_registers_is_bootstrap_register(address)) {
        switch (address) {
            case 0x00000200: // Control Channel Privilege
            case 0x00000204: // Control Channel Privilege Key
                return true;
            default:
                return false;
        }
    }
    
    // Most GenICam registers are writable
    if (genicam_registers_is_genicam_register(address)) {
        switch (address) {
            // Read-only statistics registers
            case GENICAM_TOTAL_COMMANDS_OFFSET:
            case GENICAM_TOTAL_ERRORS_OFFSET:
            case GENICAM_UNKNOWN_COMMANDS_OFFSET:
            case GENICAM_PACKETS_SENT_OFFSET:
            case GENICAM_PACKET_ERRORS_OFFSET:
            case GENICAM_FRAMES_SENT_OFFSET:
            case GENICAM_FRAME_ERRORS_OFFSET:
            case GENICAM_CONNECTION_STATUS_OFFSET:
            case GENICAM_OUT_OF_ORDER_FRAMES_OFFSET:
            case GENICAM_LOST_FRAMES_OFFSET:
            case GENICAM_DUPLICATE_FRAMES_OFFSET:
            case GENICAM_EXPECTED_SEQUENCE_OFFSET:
            case GENICAM_LAST_SEQUENCE_OFFSET:
            case GENICAM_FRAMES_IN_RING_OFFSET:
            case GENICAM_CONNECTION_FAILURES_OFFSET:
            case GENICAM_RECOVERY_MODE_OFFSET:
            case GENICAM_DISCOVERY_BROADCASTS_SENT_OFFSET:
            case GENICAM_DISCOVERY_BROADCAST_FAILURES_OFFSET:
            case GENICAM_DISCOVERY_BROADCAST_SEQUENCE_OFFSET:
                return false;
            default:
                return true;
        }
    }
    
    // Standard GVCP registers - most are writable
    return true;
}

bool genicam_registers_is_bootstrap_register(uint32_t address) {
    return address < 0x00001000;
}

bool genicam_registers_is_genicam_register(uint32_t address) {
    return address >= 0x00001000 && address < 0x00002000;
}

genicam_registers_result_t genicam_registers_read(uint32_t address, uint32_t *value) {
    if (!value) {
        return GENICAM_REGISTERS_INVALID_ARG;
    }

    if (!genicam_registers_is_address_valid(address)) {
        return GENICAM_REGISTERS_INVALID_ADDRESS;
    }

    // Handle bootstrap registers
    if (genicam_registers_is_bootstrap_register(address)) {
        if (get_bootstrap_callback == NULL) {
            platform->log_error(TAG, "Bootstrap callback not set");
            return GENICAM_REGISTERS_ERROR;
        }
        
        uint8_t *bootstrap_memory = get_bootstrap_callback();
        if (address + 4 <= 0x938) { // Bootstrap memory size check
            *value = ntohl(*(uint32_t *)(bootstrap_memory + address));
            return GENICAM_REGISTERS_SUCCESS;
        }
        return GENICAM_REGISTERS_INVALID_ADDRESS;
    }

    // Handle GenICam registers
    if (genicam_registers_is_genicam_register(address)) {
        // Handle specific registers
        switch (address) {
            case GENICAM_EXPOSURE_TIME_OFFSET:
                *value = exposure_time_us;
                break;
            case GENICAM_GAIN_OFFSET:
                *value = gain;
                break;
            case GENICAM_PIXEL_FORMAT_OFFSET:
                *value = pixel_format;
                break;
            case GENICAM_TOTAL_COMMANDS_OFFSET:
                *value = total_commands;
                break;
            case GENICAM_TOTAL_ERRORS_OFFSET:
                *value = total_errors;
                break;
            case GENICAM_UNKNOWN_COMMANDS_OFFSET:
                *value = unknown_commands;
                break;
            case GENICAM_CONNECTION_STATUS_OFFSET:
                *value = connection_status;
                break;
            default: {
                int index = get_register_index(address);
                if (index >= 0) {
                    *value = genicam_register_values[index];
                } else {
                    *value = 0;
                }
                break;
            }
        }
        return GENICAM_REGISTERS_SUCCESS;
    }

    // Handle standard GVCP registers
    switch (address) {
        case GVCP_TL_PARAMS_LOCKED_OFFSET:
            *value = tl_params_locked;
            break;
        case GVCP_GEVSCDA_DEST_ADDRESS_OFFSET:
        case GVCP_GEV_SCDA_DEST_ADDRESS_OFFSET:
            *value = stream_dest_address;
            break;
        case GVCP_GEV_N_STREAM_CHANNELS_OFFSET:
            *value = 1; // We have 1 stream channel
            break;
        case GVCP_GEV_N_NETWORK_INTERFACES_OFFSET:
            *value = 1; // We have 1 network interface
            break;
        case GVCP_GEVSC_CFG_MULTIPART_OFFSET:
            *value = multipart_enabled ? 1 : 0;
            break;
        default:
            *value = 0;
            break;
    }

    return GENICAM_REGISTERS_SUCCESS;
}

genicam_registers_result_t genicam_registers_write(uint32_t address, uint32_t value) {
    if (!genicam_registers_is_address_valid(address)) {
        return GENICAM_REGISTERS_INVALID_ADDRESS;
    }

    if (!genicam_registers_is_address_writable(address)) {
        return GENICAM_REGISTERS_WRITE_PROTECTED;
    }

    // Handle GenICam registers
    if (genicam_registers_is_genicam_register(address)) {
        switch (address) {
            case GENICAM_EXPOSURE_TIME_OFFSET:
                exposure_time_us = value;
                platform->log_info(TAG, "Exposure time set to %d us", value);
                break;
            case GENICAM_GAIN_OFFSET:
                gain = value;
                platform->log_info(TAG, "Gain set to %d", value);
                break;
            case GENICAM_PIXEL_FORMAT_OFFSET:
                pixel_format = value;
                platform->log_info(TAG, "Pixel format set to 0x%08x", value);
                break;
            case GENICAM_ACQUISITION_START_OFFSET:
                platform->log_info(TAG, "Acquisition start triggered");
                break;
            case GENICAM_ACQUISITION_STOP_OFFSET:
                platform->log_info(TAG, "Acquisition stop triggered");
                break;
            default: {
                int index = get_register_index(address);
                if (index >= 0) {
                    genicam_register_values[index] = value;
                }
                break;
            }
        }
        return GENICAM_REGISTERS_SUCCESS;
    }

    // Handle standard GVCP registers
    switch (address) {
        case GVCP_TL_PARAMS_LOCKED_OFFSET:
            tl_params_locked = value;
            break;
        case GVCP_GEVSCDA_DEST_ADDRESS_OFFSET:
        case GVCP_GEV_SCDA_DEST_ADDRESS_OFFSET:
            stream_dest_address = value;
            break;
        case GVCP_GEVSC_CFG_MULTIPART_OFFSET:
            multipart_enabled = (value != 0);
            break;
        default:
            // Ignore unknown register writes
            break;
    }

    return GENICAM_REGISTERS_SUCCESS;
}

genicam_registers_result_t genicam_registers_read_memory(uint32_t address, uint8_t *buffer, size_t length) {
    if (!buffer || length == 0) {
        return GENICAM_REGISTERS_INVALID_ARG;
    }

    // Handle bootstrap memory reads
    if (genicam_registers_is_bootstrap_register(address) && get_bootstrap_callback) {
        uint8_t *bootstrap_memory = get_bootstrap_callback();
        if (address + length <= 0x938) { // Bootstrap memory size check
            memcpy(buffer, bootstrap_memory + address, length);
            return GENICAM_REGISTERS_SUCCESS;
        }
    }

    // For other memory reads, read as 32-bit registers
    for (size_t i = 0; i < length; i += 4) {
        uint32_t value;
        genicam_registers_result_t result = genicam_registers_read(address + i, &value);
        if (result != GENICAM_REGISTERS_SUCCESS) {
            return result;
        }
        
        // Copy as many bytes as needed
        size_t copy_bytes = (length - i < 4) ? (length - i) : 4;
        uint32_t net_value = htonl(value);
        memcpy(buffer + i, &net_value, copy_bytes);
    }

    return GENICAM_REGISTERS_SUCCESS;
}

genicam_registers_result_t genicam_registers_write_memory(uint32_t address, const uint8_t *buffer, size_t length) {
    if (!buffer || length == 0) {
        return GENICAM_REGISTERS_INVALID_ARG;
    }

    // For memory writes, write as 32-bit registers
    for (size_t i = 0; i < length; i += 4) {
        uint32_t net_value = 0;
        size_t copy_bytes = (length - i < 4) ? (length - i) : 4;
        memcpy(&net_value, buffer + i, copy_bytes);
        uint32_t value = ntohl(net_value);
        
        genicam_registers_result_t result = genicam_registers_write(address + i, value);
        if (result != GENICAM_REGISTERS_SUCCESS) {
            return result;
        }
    }

    return GENICAM_REGISTERS_SUCCESS;
}

// Configuration functions
uint32_t genicam_registers_get_packet_delay_us(void) {
    uint32_t value;
    if (genicam_registers_read(GENICAM_PACKET_DELAY_OFFSET, &value) == GENICAM_REGISTERS_SUCCESS) {
        return value;
    }
    return 0;
}

float genicam_registers_get_frame_rate_fps(void) {
    uint32_t value;
    if (genicam_registers_read(GENICAM_FRAME_RATE_OFFSET, &value) == GENICAM_REGISTERS_SUCCESS) {
        return *(float*)&value; // Interpret as float
    }
    return 10.0f; // Default 10 FPS
}

uint32_t genicam_registers_get_packet_size(void) {
    uint32_t value;
    if (genicam_registers_read(GENICAM_PACKET_SIZE_OFFSET, &value) == GENICAM_REGISTERS_SUCCESS) {
        return value;
    }
    return 1400; // Default packet size
}

void genicam_registers_set_stream_status(uint32_t status) {
    genicam_registers_write(GENICAM_STREAM_STATUS_OFFSET, status);
}

uint32_t genicam_registers_get_tl_params_locked(void) {
    return tl_params_locked;
}

void genicam_registers_set_tl_params_locked(uint32_t locked) {
    tl_params_locked = locked;
}

uint32_t genicam_registers_get_stream_dest_address(void) {
    return stream_dest_address;
}

void genicam_registers_set_stream_dest_address(uint32_t dest_ip) {
    stream_dest_address = dest_ip;
}

bool genicam_registers_get_multipart_enabled(void) {
    return multipart_enabled;
}

void genicam_registers_set_multipart_enabled(bool enabled) {
    multipart_enabled = enabled;
}

uint32_t genicam_registers_get_multipart_config(void) {
    return multipart_config;
}

void genicam_registers_set_multipart_config(uint32_t config) {
    multipart_config = config;
}

uint32_t genicam_registers_get_exposure_time(void) {
    return exposure_time_us;
}

void genicam_registers_set_exposure_time(uint32_t exposure_us) {
    exposure_time_us = exposure_us;
}

uint32_t genicam_registers_get_gain(void) {
    return gain;
}

void genicam_registers_set_gain(uint32_t new_gain) {
    gain = new_gain;
}

uint32_t genicam_registers_get_pixel_format(void) {
    return pixel_format;
}

void genicam_registers_set_pixel_format(uint32_t format) {
    pixel_format = format;
}

void genicam_registers_increment_total_commands(void) {
    total_commands++;
}

void genicam_registers_increment_total_errors(void) {
    total_errors++;
}

void genicam_registers_increment_unknown_commands(void) {
    unknown_commands++;
}

uint32_t genicam_registers_get_connection_status(void) {
    return connection_status;
}

void genicam_registers_set_connection_status_bit(uint8_t bit, bool value) {
    if (value) {
        connection_status |= (1 << bit);
    } else {
        connection_status &= ~(1 << bit);
    }
}

genicam_registers_result_t genicam_registers_init(void) {
    // Initialize register values
    memset(genicam_register_values, 0, sizeof(genicam_register_values));
    
    // Set default values
    tl_params_locked = 0;
    stream_dest_address = 0;
    multipart_enabled = false;
    multipart_config = 0;
    exposure_time_us = 10000;
    gain = 100;
    pixel_format = 0x01080001; // Mono8
    
    // Clear statistics
    total_commands = 0;
    total_errors = 0;
    unknown_commands = 0;
    connection_status = 0;
    
    platform->log_info(TAG, "GenICam registers initialized");
    return GENICAM_REGISTERS_SUCCESS;
}