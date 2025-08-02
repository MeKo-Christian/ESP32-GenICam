#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Global socket reference (defined in gvcp_handler.c)
extern int sock;

#define GVCP_PORT 3956

// GigE Vision GVCP Protocol Constants
#define GVCP_MAGIC_BYTE_1 0x42 // 'B'
#define GVCP_MAGIC_BYTE_2 0x45 // 'E'

// GVCP packet type constants (GigE Vision specification)
#define GVCP_PACKET_TYPE 0x42       // Always for GVCP
#define GVCP_PACKET_FLAG_ACK 0x01   // Set for ACKs
#define GVCP_PACKET_TYPE_ERROR 0x80 // NACK/Error packet

// GVCP protocol version
#define GVCP_PROTOCOL_VERSION_1_0 0x00
#define GVCP_PROTOCOL_VERSION_1_1 0x01

// GVCP command codes (GigE Vision specification)
#define GVCP_CMD_DISCOVERY 0x0002
#define GVCP_ACK_DISCOVERY 0x0002 // Discovery responses echo the same command code
#define GVCP_CMD_PACKETRESEND 0x0040
#define GVCP_ACK_PACKETRESEND 0x0041
#define GVCP_CMD_READ_MEMORY 0x0080
#define GVCP_ACK_READ_MEMORY 0x0081
#define GVCP_CMD_READREG 0x0082
#define GVCP_ACK_READREG 0x0083
#define GVCP_CMD_WRITE_MEMORY 0x0084
#define GVCP_ACK_WRITE_MEMORY 0x0085
#define GVCP_CMD_WRITEREG 0x0086
#define GVCP_ACK_WRITEREG 0x0087

#define GVCP_FLAGS_ACK_REQUIRED 0x01

// GVCP size field utility macro - converts payload bytes to 32-bit words
#define GVCP_BYTES_TO_WORDS(bytes) (((bytes) + 3) / 4) // Ceiling division for partial words

// GVCP Error Status Codes (for NACK responses)
#define GVCP_ERROR_NOT_IMPLEMENTED 0x8001
#define GVCP_ERROR_INVALID_PARAMETER 0x8002
#define GVCP_ERROR_INVALID_ADDRESS 0x8003
#define GVCP_ERROR_WRITE_PROTECT 0x8004
#define GVCP_ERROR_BAD_ALIGNMENT 0x8005
#define GVCP_ERROR_ACCESS_DENIED 0x8006
#define GVCP_ERROR_BUSY 0x8007
#define GVCP_ERROR_MSG_TIMEOUT 0x800B
#define GVCP_ERROR_INVALID_HEADER 0x800E
#define GVCP_ERROR_WRONG_CONFIG 0x800F

// GVCP packet header structure (original format for compatibility)
typedef struct __attribute__((packed))
{
    uint8_t packet_type;
    uint8_t packet_flags;
    uint16_t command;
    uint16_t size;
    uint16_t id;
} gvcp_header_t;

// Protocol utility functions
esp_err_t gvcp_send_nack(const gvcp_header_t *original_header, uint16_t error_code, struct sockaddr_in *client_addr);
esp_err_t gvcp_sendto(const void *data, size_t data_len, struct sockaddr_in *client_addr);
bool gvcp_validate_packet_header(const gvcp_header_t *header, int packet_len);
void gvcp_create_response_header(gvcp_header_t *response, const gvcp_header_t *request, uint16_t response_command, uint16_t response_size_words);