#!/bin/bash
# ODT -> DOCX converter module
# Usage: odt_to_docx.sh <input.odt> <output.docx>

set -e

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.odt> <output.docx>" >&2
  exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if [ ! -f "$INPUT_FILE" ]; then
  echo "Error: Input file not found: $INPUT_FILE" >&2
  exit 1
fi

OUTPUT_DIR="$(dirname "$OUTPUT_FILE")"

if command -v libreoffice &> /dev/null; then
  libreoffice --headless --convert-to docx --outdir "$OUTPUT_DIR" "$INPUT_FILE" > /dev/null 2>&1
  BASENAME="$(basename "$INPUT_FILE" .odt)"
  GENERATED="$OUTPUT_DIR/$BASENAME.docx"
  if [ -f "$GENERATED" ] && [ "$GENERATED" != "$OUTPUT_FILE" ]; then
    mv "$GENERATED" "$OUTPUT_FILE"
  fi
elif command -v unoconv &> /dev/null; then
  unoconv -f docx -o "$OUTPUT_FILE" "$INPUT_FILE"
else
  echo "Error: Install LibreOffice or unoconv" >&2
  exit 1
fi

if [ -f "$OUTPUT_FILE" ]; then
  exit 0
fi

echo "Error: Conversion failed" >&2
exit 1
