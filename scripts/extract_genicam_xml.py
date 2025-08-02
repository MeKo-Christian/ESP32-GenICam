#!/usr/bin/env python3
import sys
import re
import ast

if len(sys.argv) != 3:
    print("Usage: extract_genicam_xml.py <input_c_file> <output_xml_file>")
    sys.exit(1)

input_file = sys.argv[1]
output_file = sys.argv[2]

with open(input_file, 'r', encoding='utf-8') as f:
    source = f.read()

# Find the C string content assigned to genicam_xml_data[]
match = re.search(r'genicam_xml_data\[\]\s*=\s*((?:"(?:\\.|[^"])*"\s*)+);', source, re.DOTALL)
if not match:
    print("❌ Could not find genicam_xml_data[] assignment")
    sys.exit(1)

c_string_block = match.group(1)

# Extract all string literals and join them
strings = re.findall(r'"((?:\\.|[^"])*)"', c_string_block)
joined = ''.join(strings)

try:
    decoded = ast.literal_eval(f'"{joined}"')
except Exception as e:
    print(f"❌ Failed to decode C string: {e}")
    sys.exit(1)

with open(output_file, 'w', encoding='utf-8') as f:
    f.write(decoded)

print(f"✅ XML extracted to {output_file} ({len(decoded)} bytes)")
