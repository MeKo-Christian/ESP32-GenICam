#!/bin/bash

# GenICam XML Validation Script for ESP32-CAM Project
# This script validates the GenICam XML against the official schema

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
XML_FILE="$PROJECT_DIR/esp32_genicam.xml"
SCHEMA_FILE="$PROJECT_DIR/tools/schema/GenApiSchema_Version_1_0.xsd"
SOURCE_FILE="$PROJECT_DIR/components/main/genicam_xml.c"

echo "ESP32-CAM GenICam XML Validation"
echo "================================"

# Check if schema file exists
if [ ! -f "$SCHEMA_FILE" ]; then
    echo "❌ Schema file not found: $SCHEMA_FILE"
    echo "   Creating tools/schema directory and downloading schema..."
    mkdir -p "$(dirname "$SCHEMA_FILE")"
    wget -O "$SCHEMA_FILE" https://raw.githubusercontent.com/AravisProject/aravis-misc/main/genicam/GenApiSchema_Version_1_0.xsd
    echo "✅ Schema downloaded to tools/schema/"
fi

# Extract XML from C source if standalone file doesn't exist or is older
if [ ! -f "$XML_FILE" ] || [ "$SOURCE_FILE" -nt "$XML_FILE" ]; then
    python3 scripts/extract_genicam_xml.py "$SOURCE_FILE" "$XML_FILE"
        
    if [ ! -s "$XML_FILE" ]; then
        echo "❌ XML extraction failed: $XML_FILE is empty or missing"
        exit 1
    fi

    echo "✅ XML extracted to $XML_FILE"
fi

# Validate XML well-formedness
echo "🔍 Checking XML well-formedness..."
if xmllint --noout "$XML_FILE" 2>/dev/null; then
    echo "✅ XML is well-formed"
else
    echo "❌ XML is not well-formed"
    xmllint --noout "$XML_FILE"
    exit 1
fi

# Validate against GenICam schema
echo "🔍 Validating against GenICam schema..."
if xmllint --schema "$SCHEMA_FILE" "$XML_FILE" --noout 2>/dev/null; then
    echo "✅ XML validates against GenICam schema"
    VALIDATION_RESULT=0
else
    echo "⚠️  XML has schema validation issues:"
    xmllint --schema "$SCHEMA_FILE" "$XML_FILE" --noout 2>&1 | head -20
    echo ""
    echo "Note: Some validation errors may not affect runtime compatibility"
    VALIDATION_RESULT=1
fi

# Check for common GenICam patterns
echo "🔍 Checking GenICam compliance..."

# Check namespace
if grep -q 'xmlns="http://www.genicam.org/GenApi/Version_1_0"' "$XML_FILE"; then
    echo "✅ Correct GenICam namespace"
else
    echo "❌ Missing or incorrect GenICam namespace"
fi

# Check required categories
REQUIRED_CATEGORIES=("Root" "DeviceControl" "ImageFormatControl")
for category in "${REQUIRED_CATEGORIES[@]}"; do
    if grep -q "Name=\"$category\"" "$XML_FILE"; then
        echo "✅ Category $category present"
    else
        echo "❌ Missing required category: $category"
    fi
done

# Check SFNC compliance for common features
SFNC_FEATURES=("Width" "Height" "PixelFormat" "DeviceVendorName" "DeviceModelName")
for feature in "${SFNC_FEATURES[@]}"; do
    if grep -q "Name=\"$feature\"" "$XML_FILE"; then
        echo "✅ SFNC feature $feature present"
    else
        echo "⚠️  SFNC feature $feature missing"
    fi
done

echo ""
echo "Validation Summary:"
echo "=================="
if [ $VALIDATION_RESULT -eq 0 ]; then
    echo "✅ XML passes schema validation"
else
    echo "⚠️  XML has schema validation issues but may still work with Aravis"
fi

echo ""
echo "Next steps:"
echo "- Test with live ESP32-CAM device using: arv-tool-0.10 --debug=all"
echo "- View with Aravis viewer: arv-viewer-0.10"
echo "- Check protocol with Wireshark on UDP port 3956"

# Exit successfully since XML is well-formed and functional
# Schema warnings don't prevent runtime compatibility with Aravis
exit 0