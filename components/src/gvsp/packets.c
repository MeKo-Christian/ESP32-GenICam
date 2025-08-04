#include "packets.h"
#include "../utils/platform.h"
#include <string.h>
#include <arpa/inet.h>

// static const char *TAG = "gvsp_packets"; // Unused for now

bool gvsp_packets_validate_header(const gvsp_header_t *header, size_t packet_len) {
    if (!header || packet_len < sizeof(gvsp_header_t)) {
        return false;
    }

    // Check packet type
    switch (header->packet_type) {
        case GVSP_PACKET_TYPE_DATA:
        case GVSP_PACKET_TYPE_LEADER:
        case GVSP_PACKET_TYPE_TRAILER:
            break;
        default:
            return false;
    }

    return true;
}

bool gvsp_packets_validate_leader(const gvsp_leader_data_t *leader_data) {
    if (!leader_data) {
        return false;
    }

    // Basic validation of leader data
    uint32_t size_x = ntohl(leader_data->size_x);
    uint32_t size_y = ntohl(leader_data->size_y);
    
    // Check for reasonable image dimensions
    if (size_x == 0 || size_y == 0 || size_x > 10000 || size_y > 10000) {
        return false;
    }

    return true;
}

bool gvsp_packets_validate_trailer(const gvsp_trailer_data_t *trailer_data) {
    if (!trailer_data) {
        return false;
    }

    // Basic validation of trailer data
    uint32_t size_y = ntohl(trailer_data->size_y);
    
    // Check for reasonable height
    if (size_y == 0 || size_y > 10000) {
        return false;
    }

    return true;
}

bool gvsp_packets_parse_header(const uint8_t *packet, size_t packet_len, gvsp_header_t *header) {
    if (!packet || !header || packet_len < sizeof(gvsp_header_t)) {
        return false;
    }

    memcpy(header, packet, sizeof(gvsp_header_t));
    return gvsp_packets_validate_header(header, packet_len);
}

bool gvsp_packets_parse_leader(const uint8_t *packet, size_t packet_len, gvsp_leader_data_t *leader_data) {
    if (!packet || !leader_data || packet_len < sizeof(gvsp_header_t) + sizeof(gvsp_leader_data_t)) {
        return false;
    }

    gvsp_header_t header;
    if (!gvsp_packets_parse_header(packet, packet_len, &header)) {
        return false;
    }

    if (header.packet_type != GVSP_PACKET_TYPE_LEADER) {
        return false;
    }

    memcpy(leader_data, packet + sizeof(gvsp_header_t), sizeof(gvsp_leader_data_t));
    return gvsp_packets_validate_leader(leader_data);
}

bool gvsp_packets_parse_trailer(const uint8_t *packet, size_t packet_len, gvsp_trailer_data_t *trailer_data) {
    if (!packet || !trailer_data || packet_len < sizeof(gvsp_header_t) + sizeof(gvsp_trailer_data_t)) {
        return false;
    }

    gvsp_header_t header;
    if (!gvsp_packets_parse_header(packet, packet_len, &header)) {
        return false;
    }

    if (header.packet_type != GVSP_PACKET_TYPE_TRAILER) {
        return false;
    }

    memcpy(trailer_data, packet + sizeof(gvsp_header_t), sizeof(gvsp_trailer_data_t));
    return gvsp_packets_validate_trailer(trailer_data);
}

uint16_t gvsp_packets_get_packet_id(const gvsp_header_t *header) {
    return header ? ntohs(header->packet_id) : 0;
}

uint32_t gvsp_packets_get_block_id(const gvsp_header_t *header) {
    return header ? ntohl(header->data[0]) : 0;
}

uint32_t gvsp_packets_get_data_offset(const gvsp_header_t *header) {
    return header ? ntohl(header->data[1]) : 0;
}

uint8_t gvsp_packets_get_packet_type(const gvsp_header_t *header) {
    return header ? header->packet_type : 0xFF;
}

size_t gvsp_packets_calculate_leader_size(void) {
    return sizeof(gvsp_header_t) + sizeof(gvsp_leader_data_t);
}

size_t gvsp_packets_calculate_trailer_size(void) {
    return sizeof(gvsp_header_t) + sizeof(gvsp_trailer_data_t);
}

size_t gvsp_packets_calculate_data_size(size_t data_len) {
    return sizeof(gvsp_header_t) + data_len;
}

size_t gvsp_packets_calculate_total_packets(size_t frame_size) {
    // Leader + data packets + trailer
    size_t data_packets = (frame_size + GVSP_DATA_PACKET_SIZE - 1) / GVSP_DATA_PACKET_SIZE;
    return 1 + data_packets + 1; // 1 leader + data packets + 1 trailer
}

const char* gvsp_packets_get_pixel_format_name(uint32_t pixel_format) {
    switch (pixel_format) {
        case GVSP_PIXEL_MONO8:
            return "Mono8";
        case GVSP_PIXEL_RGB565:
            return "RGB565";
        case GVSP_PIXEL_YUV422:
            return "YUV422";
        case GVSP_PIXEL_RGB888:
            return "RGB888";
        case GVSP_PIXEL_JPEG:
            return "JPEG";
        default:
            return "Unknown";
    }
}

uint32_t gvsp_packets_get_bytes_per_pixel(uint32_t pixel_format) {
    switch (pixel_format) {
        case GVSP_PIXEL_MONO8:
            return 1;
        case GVSP_PIXEL_RGB565:
        case GVSP_PIXEL_YUV422:
            return 2;
        case GVSP_PIXEL_RGB888:
            return 3;
        case GVSP_PIXEL_JPEG:
            return 0; // Variable/compressed
        default:
            return 0;
    }
}

bool gvsp_packets_is_compressed_format(uint32_t pixel_format) {
    switch (pixel_format) {
        case GVSP_PIXEL_JPEG:
            return true;
        default:
            return false;
    }
}