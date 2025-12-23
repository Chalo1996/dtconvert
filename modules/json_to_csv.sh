#!/bin/bash
# JSON -> CSV converter
# Usage: json_to_csv.sh <input.json> <output.csv>

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.json> <output.csv>" >&2
  exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if [ ! -f "$INPUT_FILE" ]; then
  echo "Error: Input file not found: $INPUT_FILE" >&2
  exit 1
fi

"$(dirname "$0")/../lib/converters/data_convert" "$INPUT_FILE" "$OUTPUT_FILE"
