#!/bin/bash
# CSV -> XLSX converter
# Usage: csv_to_xlsx.sh <input.csv> <output.xlsx>

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.csv> <output.xlsx>" >&2
  exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if [ ! -f "$INPUT_FILE" ]; then
  echo "Error: Input file not found: $INPUT_FILE" >&2
  exit 1
fi

OUTDIR="$(dirname "$OUTPUT_FILE")"
mkdir -p "$OUTDIR"

if command -v libreoffice >/dev/null 2>&1; then
  # LibreOffice infers the output name from input, so we may need to rename.
  libreoffice --headless --convert-to xlsx --outdir "$OUTDIR" "$INPUT_FILE" >/dev/null 2>&1
  in_base="$(basename "$INPUT_FILE")"
  stem="${in_base%.*}"
  generated="$OUTDIR/$stem.xlsx"
  if [ -f "$generated" ] && [ "$generated" != "$OUTPUT_FILE" ]; then
    mv "$generated" "$OUTPUT_FILE"
  fi
elif command -v ssconvert >/dev/null 2>&1; then
  ssconvert "$INPUT_FILE" "$OUTPUT_FILE" >/dev/null 2>&1
else
  echo "Error: Need libreoffice or ssconvert (gnumeric) for CSV<->XLSX conversion" >&2
  exit 1
fi

if [ ! -f "$OUTPUT_FILE" ]; then
  echo "Error: Conversion failed" >&2
  exit 1
fi
