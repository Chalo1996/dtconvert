#!/bin/bash
# XLSX -> CSV converter
# Usage: xlsx_to_csv.sh <input.xlsx> <output.csv>

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.xlsx> <output.csv>" >&2
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

if command -v xlsx2csv >/dev/null 2>&1; then
  xlsx2csv "$INPUT_FILE" "$OUTPUT_FILE" >/dev/null 2>&1
elif command -v libreoffice >/dev/null 2>&1; then
  # LibreOffice will generate <input>.csv by default (possibly for first sheet)
  libreoffice --headless --convert-to csv --outdir "$OUTDIR" "$INPUT_FILE" >/dev/null 2>&1
  in_base="$(basename "$INPUT_FILE")"
  stem="${in_base%.*}"
  generated="$OUTDIR/$stem.csv"
  if [ -f "$generated" ] && [ "$generated" != "$OUTPUT_FILE" ]; then
    mv "$generated" "$OUTPUT_FILE"
  fi
elif command -v ssconvert >/dev/null 2>&1; then
  ssconvert "$INPUT_FILE" "$OUTPUT_FILE" >/dev/null 2>&1
else
  echo "Error: Need xlsx2csv, libreoffice, or ssconvert (gnumeric) for CSV<->XLSX conversion" >&2
  exit 1
fi

if [ ! -f "$OUTPUT_FILE" ]; then
  echo "Error: Conversion failed" >&2
  exit 1
fi
