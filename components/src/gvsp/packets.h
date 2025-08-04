#ifndef GVSP_PACKETS_H
#define GVSP_PACKETS_H

#include "streaming.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Packet validation functions
bool gvsp_packets_validate_header(const gvsp_header_t *header, size_t packet_len);
bool gvsp_packets_validate_leader(const gvsp_leader_data_t *leader_data);
bool gvsp_packets_validate_trailer(const gvsp_trailer_data_t *trailer_data);

// Packet parsing functions
bool gvsp_packets_parse_header(const uint8_t *packet, size_t packet_len, gvsp_header_t *header);
bool gvsp_packets_parse_leader(const uint8_t *packet, size_t packet_len, gvsp_leader_data_t *leader_data);
bool gvsp_packets_parse_trailer(const uint8_t *packet, size_t packet_len, gvsp_trailer_data_t *trailer_data);

// Packet utility functions
uint16_t gvsp_packets_get_packet_id(const gvsp_header_t *header);
uint32_t gvsp_packets_get_block_id(const gvsp_header_t *header);
uint32_t gvsp_packets_get_data_offset(const gvsp_header_t *header);
uint8_t gvsp_packets_get_packet_type(const gvsp_header_t *header);

// Packet size calculations
size_t gvsp_packets_calculate_leader_size(void);
size_t gvsp_packets_calculate_trailer_size(void);
size_t gvsp_packets_calculate_data_size(size_t data_len);
size_t gvsp_packets_calculate_total_packets(size_t frame_size);

// Pixel format utilities
const char* gvsp_packets_get_pixel_format_name(uint32_t pixel_format);
uint32_t gvsp_packets_get_bytes_per_pixel(uint32_t pixel_format);
bool gvsp_packets_is_compressed_format(uint32_t pixel_format);

#endif // GVSP_PACKETS_H