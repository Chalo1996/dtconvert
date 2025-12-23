#!/bin/bash
# CSV -> PDF converter
# Usage: csv_to_pdf.sh <input.csv> <output.pdf>
#
# Implementation: CSV -> TXT (pretty) -> PDF.

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.csv> <output.pdf>" >&2
  exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if [ ! -f "$INPUT_FILE" ]; then
  echo "Error: Input file not found: $INPUT_FILE" >&2
  exit 1
fi

MODULE_DIR="$(cd "$(dirname "$0")" && pwd)"

TMP_TXT="$(mktemp /tmp/dtconvert_csv_to_pdf.XXXXXX.txt)"
cleanup() {
  rm -f "$TMP_TXT"
}
trap cleanup EXIT

"$MODULE_DIR/csv_to_txt.sh" "$INPUT_FILE" "$TMP_TXT"
"$MODULE_DIR/txt_to_pdf.sh" "$TMP_TXT" "$OUTPUT_FILE"
