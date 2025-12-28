#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT_DIR/src/conversion.c"
README="$ROOT_DIR/README.md"

if [[ ! -f "$SRC" ]]; then
  echo "Error: missing $SRC" >&2
  exit 1
fi
if [[ ! -f "$README" ]]; then
  echo "Error: missing $README" >&2
  exit 1
fi

start_marker='<!-- BEGIN SUPPORTED_CONVERSIONS (autogen) -->'
end_marker='<!-- END SUPPORTED_CONVERSIONS (autogen) -->'

# Extract registry entries of the form {"from", "to", "path", "desc"}
# and output a stable, simple markdown table.
rows="$(
  awk '
    BEGIN { FS="\"" }
    /\{"[a-z0-9]+"[[:space:]]*,[[:space:]]*"[a-z0-9]+"[[:space:]]*,[[:space:]]*"[^\"]+"[[:space:]]*,[[:space:]]*"[^\"]+"\}/ {
      from=$2; to=$4; path=$6; desc=$8;
      if (from != "" && to != "" && path != "" && desc != "") {
        printf("%s\t%s\t%s\t%s\n", from, to, path, desc);
      }
    }
  ' "$SRC" | LC_ALL=C sort -t $'\t' -k1,1 -k2,2
)"

{
  echo "$start_marker"
  echo
  echo "| From | To | Implementation |"
  echo "|------|----|----------------|"
  if [[ -n "$rows" ]]; then
    while IFS=$'\t' read -r from to path desc; do
      # Keep it compact; description lives in code.
      printf '| %s | %s | %s |\n' "$from" "$to" "$path"
    done <<< "$rows"
  fi
  echo
  echo "$end_marker"
} >"$ROOT_DIR/.supported_conversions.tmp"

# Replace existing block if present, otherwise insert after "## Usage".
if grep -Fq "$start_marker" "$README" && grep -Fq "$end_marker" "$README"; then
  awk -v s="$start_marker" -v e="$end_marker" -v repl_file="$ROOT_DIR/.supported_conversions.tmp" '
    BEGIN {
      while ((getline line < repl_file) > 0) repl = repl line "\n";
      close(repl_file);
      inblock=0;
    }
    {
      if ($0 == s) { printf "%s", repl; inblock=1; next }
      if (inblock && $0 == e) { inblock=0; next }
      if (!inblock) print
    }
  ' "$README" >"$ROOT_DIR/.README.tmp"
  mv "$ROOT_DIR/.README.tmp" "$README"
else
  awk -v repl_file="$ROOT_DIR/.supported_conversions.tmp" '
    BEGIN {
      while ((getline line < repl_file) > 0) repl = repl line "\n";
      close(repl_file);
      inserted=0;
    }
    {
      print
      if (!inserted && $0 == "## Usage") {
        print ""
        print "## Supported conversions"
        print ""
        printf "%s", repl
        inserted=1
      }
    }
    END {
      if (!inserted) {
        print ""
        print "## Supported conversions"
        print ""
        printf "%s", repl
      }
    }
  ' "$README" >"$ROOT_DIR/.README.tmp"
  mv "$ROOT_DIR/.README.tmp" "$README"
fi

rm -f "$ROOT_DIR/.supported_conversions.tmp"

echo "Updated README supported conversions table."
