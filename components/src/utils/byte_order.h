#ifndef BYTE_ORDER_H
#define BYTE_ORDER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Platform-independent byte order conversion functions
uint16_t platform_htons(uint16_t hostshort);
uint32_t platform_htonl(uint32_t hostlong);
uint16_t platform_ntohs(uint16_t netshort);
uint32_t platform_ntohl(uint32_t netlong);

// Host byte order detection
bool platform_is_big_endian(void);
bool platform_is_little_endian(void);

// Byte swap utilities
uint16_t platform_bswap16(uint16_t value);
uint32_t platform_bswap32(uint32_t value);
uint64_t platform_bswap64(uint64_t value);

// Buffer byte order conversion
void platform_convert_buffer_to_network_order(void *buffer, size_t element_size, size_t num_elements);
void platform_convert_buffer_from_network_order(void *buffer, size_t element_size, size_t num_elements);

#endif // BYTE_ORDER_H