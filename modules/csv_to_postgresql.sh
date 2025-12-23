#!/bin/bash
# CSV -> PostgreSQL importer
# Usage: csv_to_postgresql.sh <input.csv> <config.json>
#
# The config JSON file is passed via dtconvert's -o/--output.

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <input.csv> <config.json>" >&2
  exit 1
fi

INPUT_FILE="$1"
CONFIG_FILE="$2"

if [ ! -f "$INPUT_FILE" ]; then
  echo "Error: Input file not found: $INPUT_FILE" >&2
  exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
  echo "Error: Config file not found: $CONFIG_FILE" >&2
  exit 1
fi

PG_STORE_BIN="$(dirname "$0")/../lib/converters/pg_store"
if [ ! -x "$PG_STORE_BIN" ]; then
  echo "Error: pg_store helper not found or not executable: $PG_STORE_BIN" >&2
  echo "Hint: run 'make' to build helper converters" >&2
  exit 1
fi

"$PG_STORE_BIN" csv-to-postgresql "$INPUT_FILE" "$CONFIG_FILE"
