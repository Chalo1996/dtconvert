#!/bin/bash
# ODT to PDF converter
if [ $# -lt 2 ]; then
    echo "Usage: $0 <input.odt> <output.pdf>"
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if command -v libreoffice &> /dev/null; then
    libreoffice --headless --convert-to pdf --outdir "$(dirname "$OUTPUT_FILE")" "$INPUT_FILE"
    # Rename to match requested output
    BASENAME=$(basename "$INPUT_FILE" .odt)
    if [ -f "$(dirname "$OUTPUT_FILE")/$BASENAME.pdf" ]; then
        mv "$(dirname "$OUTPUT_FILE")/$BASENAME.pdf" "$OUTPUT_FILE"
    fi
elif command -v unoconv &> /dev/null; then
    unoconv -f pdf -o "$OUTPUT_FILE" "$INPUT_FILE"
else
    echo "Error: Install LibreOffice or unoconv"
    exit 1
fi