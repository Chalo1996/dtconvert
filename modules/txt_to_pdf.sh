#!/bin/bash
# Text to PDF converter using enscript and ps2pdf

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <input.txt> <output.pdf>" >&2
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if command -v enscript >/dev/null 2>&1 && command -v ps2pdf >/dev/null 2>&1; then
    TEMP_PS="$(mktemp /tmp/dtconvert_txt_to_pdf.XXXXXX.ps)"
    cleanup() {
        rm -f "$TEMP_PS"
    }
    trap cleanup EXIT

    enscript -B -p "$TEMP_PS" "$INPUT_FILE"
    ps2pdf "$TEMP_PS" "$OUTPUT_FILE"
else
    echo "Error: Install enscript and ghostscript" >&2
    exit 1
fi