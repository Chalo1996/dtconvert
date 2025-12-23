#!/bin/bash
# CSV to Text converter
if [ $# -lt 2 ]; then
    echo "Usage: $0 <input.csv> <output.txt>"
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

# Simple CSV to formatted text using column command
if command -v column &> /dev/null; then
    column -t -s ',' "$INPUT_FILE" > "$OUTPUT_FILE"
else
    # Fallback: replace commas with tabs
    sed 's/,/\t/g' "$INPUT_FILE" > "$OUTPUT_FILE"
fi