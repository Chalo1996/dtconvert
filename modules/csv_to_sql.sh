#!/bin/bash
# CSV -> SQL (INSERT statements) converter
# Usage: csv_to_sql.sh <input.csv> <output.sql>
# Optional env:
#   DTCONVERT_SQL_TABLE=name
#   DTCONVERT_SQL_CREATE=1

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.csv> <output.sql>" >&2
  exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="$2"

if [ ! -f "$INPUT_FILE" ]; then
  echo "Error: Input file not found: $INPUT_FILE" >&2
  exit 1
fi

SQL_CONVERT_BIN="$(dirname "$0")/../lib/converters/sql_convert"
if [ ! -x "$SQL_CONVERT_BIN" ]; then
  echo "Error: sql_convert helper not found or not executable: $SQL_CONVERT_BIN" >&2
  echo "Hint: run 'make' to build helper converters" >&2
  exit 1
fi

TABLE_NAME="${DTCONVERT_SQL_TABLE:-}"
if [ -z "$TABLE_NAME" ]; then
  # Default: derive from output file basename
  base="$(basename "$OUTPUT_FILE")"
  TABLE_NAME="${base%.*}"
  TABLE_NAME="${TABLE_NAME//[^A-Za-z0-9_]/_}"
  if [[ ! "$TABLE_NAME" =~ ^[A-Za-z_] ]]; then
    TABLE_NAME="data"
  fi
fi

ARGS=("--table" "$TABLE_NAME")
if [ "${DTCONVERT_SQL_CREATE:-0}" = "1" ]; then
  ARGS+=("--create")
fi

"$SQL_CONVERT_BIN" csv-to-sql "$INPUT_FILE" "$OUTPUT_FILE" "${ARGS[@]}"
