#!/bin/bash
# PostgreSQL -> CSV exporter
# Usage: postgresql_to_csv.sh <config.json> <output.csv>
#
# The input is a JSON config file describing the PostgreSQL connection and what to export.
# The output is a CSV file path.

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <config.json> <output.csv>" >&2
  exit 1
fi

CONFIG_FILE="$1"
OUTPUT_FILE="$2"

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

"$PG_STORE_BIN" postgresql-to-csv "$CONFIG_FILE" "$OUTPUT_FILE"
