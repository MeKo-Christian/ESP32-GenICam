#include "byte_order.h"
#include <string.h>

// Simple byte order detection
bool platform_is_big_endian(void) {
    union {
        uint32_t i;
        uint8_t c[4];
    } test = {0x01020304};
    
    return test.c[0] == 1;
}

bool platform_is_little_endian(void) {
    return !platform_is_big_endian();
}

// Byte swap utilities
uint16_t platform_bswap16(uint16_t value) {
    return ((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8);
}

uint32_t platform_bswap32(uint32_t value) {
    return ((value & 0x000000FF) << 24) |
           ((value & 0x0000FF00) <<  8) |
           ((value & 0x00FF0000) >>  8) |
           ((value & 0xFF000000) >> 24);
}

uint64_t platform_bswap64(uint64_t value) {
    return ((value & 0x00000000000000FFULL) << 56) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x00000000FF000000ULL) <<  8) |
           ((value & 0x000000FF00000000ULL) >>  8) |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0xFF00000000000000ULL) >> 56);
}

// Network byte order conversion (big endian)
uint16_t platform_htons(uint16_t hostshort) {
    if (platform_is_little_endian()) {
        return platform_bswap16(hostshort);
    }
    return hostshort;
}

uint32_t platform_htonl(uint32_t hostlong) {
    if (platform_is_little_endian()) {
        return platform_bswap32(hostlong);
    }
    return hostlong;
}

uint16_t platform_ntohs(uint16_t netshort) {
    if (platform_is_little_endian()) {
        return platform_bswap16(netshort);
    }
    return netshort;
}

uint32_t platform_ntohl(uint32_t netlong) {
    if (platform_is_little_endian()) {
        return platform_bswap32(netlong);
    }
    return netlong;
}

// Buffer conversion utilities
void platform_convert_buffer_to_network_order(void *buffer, size_t element_size, size_t num_elements) {
    if (!buffer || element_size == 0 || num_elements == 0) {
        return;
    }
    
    uint8_t *byte_buffer = (uint8_t *)buffer;
    
    for (size_t i = 0; i < num_elements; i++) {
        void *element = byte_buffer + (i * element_size);
        
        switch (element_size) {
            case 2: {
                uint16_t *val = (uint16_t *)element;
                *val = platform_htons(*val);
                break;
            }
            case 4: {
                uint32_t *val = (uint32_t *)element;
                *val = platform_htonl(*val);
                break;
            }
            case 8: {
                uint64_t *val = (uint64_t *)element;
                if (platform_is_little_endian()) {
                    *val = platform_bswap64(*val);
                }
                break;
            }
            default:
                // For other sizes, swap bytes manually
                if (platform_is_little_endian()) {
                    uint8_t *bytes = (uint8_t *)element;
                    for (size_t j = 0; j < element_size / 2; j++) {
                        uint8_t temp = bytes[j];
                        bytes[j] = bytes[element_size - 1 - j];
                        bytes[element_size - 1 - j] = temp;
                    }
                }
                break;
        }
    }
}

void platform_convert_buffer_from_network_order(void *buffer, size_t element_size, size_t num_elements) {
    if (!buffer || element_size == 0 || num_elements == 0) {
        return;
    }
    
    uint8_t *byte_buffer = (uint8_t *)buffer;
    
    for (size_t i = 0; i < num_elements; i++) {
        void *element = byte_buffer + (i * element_size);
        
        switch (element_size) {
            case 2: {
                uint16_t *val = (uint16_t *)element;
                *val = platform_ntohs(*val);
                break;
            }
            case 4: {
                uint32_t *val = (uint32_t *)element;
                *val = platform_ntohl(*val);
                break;
            }
            case 8: {
                uint64_t *val = (uint64_t *)element;
                if (platform_is_little_endian()) {
                    *val = platform_bswap64(*val);
                }
                break;
            }
            default:
                // For other sizes, swap bytes manually
                if (platform_is_little_endian()) {
                    uint8_t *bytes = (uint8_t *)element;
                    for (size_t j = 0; j < element_size / 2; j++) {
                        uint8_t temp = bytes[j];
                        bytes[j] = bytes[element_size - 1 - j];
                        bytes[element_size - 1 - j] = temp;
                    }
                }
                break;
        }
    }
}