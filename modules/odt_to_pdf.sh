#!/bin/bash
# ODT to PDF converter
if [ $# -lt 2 ]; then
    echo "Usage: $0 <input.odt> <output.pdf>"
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if command -v libreoffice &> /dev/null; then
    OUTDIR="$(dirname "$OUTPUT_FILE")"
    libreoffice --headless --convert-to pdf --outdir "$OUTDIR" "$INPUT_FILE"

    # LibreOffice always writes <input_basename>.pdf into OUTDIR.
    BASENAME="$(basename "$INPUT_FILE")"
    BASENAME="${BASENAME%.*}"
    PRODUCED_PDF="$OUTDIR/$BASENAME.pdf"

    if [ ! -f "$PRODUCED_PDF" ]; then
        echo "Error: LibreOffice did not produce expected output: $PRODUCED_PDF" >&2
        exit 1
    fi

    # If the requested output matches what LibreOffice produced, there's nothing to rename.
    if [ "$PRODUCED_PDF" != "$OUTPUT_FILE" ]; then
        mv -f "$PRODUCED_PDF" "$OUTPUT_FILE"
    fi
elif command -v unoconv &> /dev/null; then
    unoconv -f pdf -o "$OUTPUT_FILE" "$INPUT_FILE"
else
    echo "Error: Install LibreOffice or unoconv"
    exit 1
fi