#!/bin/bash
# DOCX to PDF converter module
# Usage: docx_to_pdf.sh <input.docx> <output.pdf>

set -e

# Check for required arguments
if [ $# -lt 2 ]; then
    echo "Usage: $0 <input.docx> <output.pdf>"
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

# Check if input file exists
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file not found: $INPUT_FILE"
    exit 1
fi

# Check if LibreOffice is available for conversion
if command -v libreoffice &> /dev/null; then
    echo "Converting DOCX to PDF using LibreOffice..."
    
    # Get output directory and filename
    OUTPUT_DIR=$(dirname "$OUTPUT_FILE")
    OUTPUT_BASENAME=$(basename "$OUTPUT_FILE" .pdf)
    
    # Convert using LibreOffice
    libreoffice --headless --convert-to pdf --outdir "$OUTPUT_DIR" "$INPUT_FILE" > /dev/null 2>&1
    
    # Rename the output file if needed
    INPUT_BASENAME=$(basename "$INPUT_FILE" .docx)
    GENERATED_PDF="$OUTPUT_DIR/$INPUT_BASENAME.pdf"
    
    if [ -f "$GENERATED_PDF" ] && [ "$GENERATED_PDF" != "$OUTPUT_FILE" ]; then
        mv "$GENERATED_PDF" "$OUTPUT_FILE"
    fi
    
elif command -v unoconv &> /dev/null; then
    # Fallback to unoconv
    echo "Converting DOCX to PDF using unoconv..."
    unoconv -f pdf -o "$OUTPUT_FILE" "$INPUT_FILE"
    
elif command -v pandoc &> /dev/null; then
    # Fallback to pandoc (requires LaTeX for PDF output)
    echo "Converting DOCX to PDF using pandoc..."
    pandoc "$INPUT_FILE" -o "$OUTPUT_FILE"
    
else
    echo "Error: No DOCX to PDF converter found. Please install LibreOffice, unoconv, or pandoc."
    exit 1
fi

# Check if conversion was successful
if [ -f "$OUTPUT_FILE" ]; then
    echo "Conversion successful: $OUTPUT_FILE"
    exit 0
else
    echo "Error: Conversion failed"
    exit 1
fi