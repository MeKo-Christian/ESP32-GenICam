#ifndef GENICAM_XML_H
#define GENICAM_XML_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// GenICam XML data access
const uint8_t* genicam_xml_get_data(void);
size_t genicam_xml_get_size(void);

// XML generation and manipulation (for future extensibility)
typedef enum {
    GENICAM_XML_SUCCESS = 0,
    GENICAM_XML_ERROR = -1,
    GENICAM_XML_INVALID_ARG = -2,
    GENICAM_XML_BUFFER_TOO_SMALL = -3
} genicam_xml_result_t;

// XML initialization
genicam_xml_result_t genicam_xml_init(void);

// XML validation (if needed for testing)
bool genicam_xml_validate(void);

#endif // GENICAM_XML_H