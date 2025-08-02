#include "gvcp_statistics.h"
#include "esp_log.h"

static const char *TAG = "gvcp_statistics";

// Error handling statistics
static uint32_t total_commands_received = 0;
static uint32_t total_errors_sent = 0;
static uint32_t total_unknown_commands = 0;

// Socket health monitoring
static uint32_t gvcp_socket_error_count = 0;
static uint32_t gvcp_max_socket_errors = 3;
static uint32_t gvcp_last_socket_recreation = 0;
static uint32_t gvcp_socket_recreation_interval_ms = 15000; // Min 15s between recreations

// Connection status (bit field)
// Bit 0: GVCP socket active
// Bit 1: GVSP socket active
// Bit 2: Client connected
// Bit 3: Streaming active
static uint32_t connection_status = 0;

// Error statistics functions
uint32_t gvcp_get_total_commands_received(void)
{
    return total_commands_received;
}

uint32_t gvcp_get_total_errors_sent(void)
{
    return total_errors_sent;
}

uint32_t gvcp_get_total_unknown_commands(void)
{
    return total_unknown_commands;
}

// Error statistics tracking
void gvcp_increment_total_commands(void)
{
    total_commands_received++;
}

void gvcp_increment_total_errors(void)
{
    total_errors_sent++;
}

void gvcp_increment_unknown_commands(void)
{
    total_unknown_commands++;
}

// Connection status management
void gvcp_set_connection_status_bit(uint8_t bit_position, bool value)
{
    if (bit_position < 32)
    {
        if (value)
        {
            connection_status |= (1U << bit_position);
        }
        else
        {
            connection_status &= ~(1U << bit_position);
        }
        ESP_LOGD(TAG, "Connection status bit %d set to %d, status: 0x%08x",
                 bit_position, value ? 1 : 0, connection_status);
    }
}

uint32_t gvcp_get_connection_status(void)
{
    return connection_status;
}

// Socket health monitoring
uint32_t gvcp_get_socket_error_count(void)
{
    return gvcp_socket_error_count;
}

void gvcp_increment_socket_error_count(void)
{
    gvcp_socket_error_count++;
    ESP_LOGD(TAG, "Socket error count incremented to %d", gvcp_socket_error_count);
}

void gvcp_reset_socket_error_count(void)
{
    if (gvcp_socket_error_count > 0)
    {
        ESP_LOGD(TAG, "Socket error count reset from %d to 0", gvcp_socket_error_count);
        gvcp_socket_error_count = 0;
    }
}

bool gvcp_should_recreate_socket(void)
{
    if (gvcp_socket_error_count >= gvcp_max_socket_errors)
    {
        uint32_t current_time = esp_log_timestamp();
        if (current_time - gvcp_last_socket_recreation >= gvcp_socket_recreation_interval_ms)
        {
            return true;
        }
    }
    return false;
}

void gvcp_update_socket_recreation_time(void)
{
    gvcp_last_socket_recreation = esp_log_timestamp();
    gvcp_socket_error_count = 0;
    ESP_LOGI(TAG, "Socket recreation time updated, error count reset");
}

// Statistics initialization
esp_err_t gvcp_statistics_init(void)
{
    // Reset all statistics
    total_commands_received = 0;
    total_errors_sent = 0;
    total_unknown_commands = 0;
    gvcp_socket_error_count = 0;
    gvcp_last_socket_recreation = 0;
    connection_status = 0;

    ESP_LOGI(TAG, "Statistics module initialized");
    return ESP_OK;
}

void gvcp_statistics_reset(void)
{
    total_commands_received = 0;
    total_errors_sent = 0;
    total_unknown_commands = 0;
    gvcp_socket_error_count = 0;
    gvcp_last_socket_recreation = 0;

    ESP_LOGI(TAG, "Statistics reset");
}