#!/bin/bash
# TXT -> tokens converter
# Usage: txt_to_tokens.sh <input.txt> <output.(txt|json)>

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.txt> <output.(txt|json)>" >&2
  exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if [ ! -f "$INPUT_FILE" ]; then
  echo "Error: Input file not found: $INPUT_FILE" >&2
  exit 1
fi

"$(dirname "$0")/../lib/converters/tokenize" "$INPUT_FILE" "$OUTPUT_FILE"
