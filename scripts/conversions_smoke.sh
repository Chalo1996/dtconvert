#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTCONVERT="$ROOT_DIR/bin/dtconvert"

if [[ ! -x "$DTCONVERT" ]]; then
  echo "Error: dtconvert not built at $DTCONVERT" >&2
  echo "Hint: run 'make' first" >&2
  exit 1
fi

echo "== Tooling availability =="
need_cmd() { command -v "$1" >/dev/null 2>&1; }
report() { printf '%-12s %s\n' "$1" "$2"; }

need_cmd curl && report curl OK || report curl MISSING
need_cmd xdg-open && report xdg-open OK || report xdg-open MISSING
need_cmd libreoffice && report libreoffice OK || report libreoffice MISSING
need_cmd unoconv && report unoconv OK || report unoconv MISSING
need_cmd pandoc && report pandoc OK || report pandoc MISSING
need_cmd enscript && report enscript OK || report enscript MISSING
need_cmd ps2pdf && report ps2pdf OK || report ps2pdf MISSING
need_cmd xlsx2csv && report xlsx2csv OK || report xlsx2csv MISSING
need_cmd ssconvert && report ssconvert OK || report ssconvert MISSING
need_cmd psql && report psql OK || report psql MISSING

[[ -n "${PGPASSWORD:-}" ]] && report PGPASSWORD SET || report PGPASSWORD MISSING
[[ -f "${HOME:-}/.pgpass" ]] && report .pgpass PRESENT || report .pgpass MISSING

echo

tmpdir="$(mktemp -d /tmp/dtconvert_conversions.XXXXXX)"
trap 'rm -rf "$tmpdir"' EXIT

# Inputs
cat >"$tmpdir/in.csv" <<'CSV'
name,age
Alice,30
Bob,41
CSV

cat >"$tmpdir/in.json" <<'JSON'
[{"name":"Alice","age":30},{"name":"Bob","age":41}]
JSON

cat >"$tmpdir/in.yaml" <<'YAML'
- name: Alice
  age: 30
- name: Bob
  age: 41
YAML

cat >"$tmpdir/in.txt" <<'TXT'
hello world
this is a test
TXT

pass=0
fail=0
skip=0

run() {
  local name="$1"; shift
  echo "-- $name"
  if "$@"; then
    echo "   PASS"
    pass=$((pass+1))
  else
    local rc=$?
    echo "   FAIL (rc=$rc)"
    fail=$((fail+1))
  fi
}

run_and_check_nonempty() {
  local name="$1"; shift
  local out="$1"; shift
  run "$name" "$@"
  if [[ -f "$out" && -s "$out" ]]; then
    :
  else
    echo "   FAIL (output missing/empty: $out)"
    fail=$((fail+1))
  fi
}

skip_test() {
  local name="$1" reason="$2"
  echo "-- $name"
  echo "   SKIP: $reason"
  skip=$((skip+1))
}

# CSV <-> JSON
run_and_check_nonempty "csv_to_json" "$tmpdir/out.csv.json" "$DTCONVERT" "$tmpdir/in.csv" --to json -o "$tmpdir/out.csv.json" -f
run_and_check_nonempty "json_to_csv" "$tmpdir/out.json.csv" "$DTCONVERT" "$tmpdir/out.csv.json" --from json --to csv -o "$tmpdir/out.json.csv" -f

# JSON <-> YAML
run_and_check_nonempty "json_to_yaml" "$tmpdir/out.json.yaml" "$DTCONVERT" "$tmpdir/in.json" --to yaml -o "$tmpdir/out.json.yaml" -f
run_and_check_nonempty "yaml_to_json" "$tmpdir/out.yaml.json" "$DTCONVERT" "$tmpdir/in.yaml" --to json -o "$tmpdir/out.yaml.json" -f

# CSV <-> YAML
run_and_check_nonempty "csv_to_yaml" "$tmpdir/out.csv.yaml" "$DTCONVERT" "$tmpdir/in.csv" --to yaml -o "$tmpdir/out.csv.yaml" -f
run_and_check_nonempty "yaml_to_csv" "$tmpdir/out.yaml.csv" "$DTCONVERT" "$tmpdir/in.yaml" --to csv -o "$tmpdir/out.yaml.csv" -f

# CSV -> TXT
run_and_check_nonempty "csv_to_txt" "$tmpdir/out.csv.txt" "$DTCONVERT" "$tmpdir/in.csv" --to txt -o "$tmpdir/out.csv.txt" -f

# TXT -> TOKENS
run_and_check_nonempty "txt_to_tokens" "$tmpdir/out.tokens" "$DTCONVERT" "$tmpdir/in.txt" --to tokens -o "$tmpdir/out.tokens" -f

# CSV <-> SQL
run_and_check_nonempty "csv_to_sql" "$tmpdir/out.csv.sql" "$DTCONVERT" "$tmpdir/in.csv" --to sql -o "$tmpdir/out.csv.sql" -f
run_and_check_nonempty "sql_to_csv" "$tmpdir/out.sql.csv" "$DTCONVERT" "$tmpdir/out.csv.sql" --from sql --to csv -o "$tmpdir/out.sql.csv" -f

# XLSX <-> CSV
if need_cmd xlsx2csv || need_cmd libreoffice || need_cmd ssconvert; then
  run_and_check_nonempty "csv_to_xlsx" "$tmpdir/out.xlsx" "$DTCONVERT" "$tmpdir/in.csv" --to xlsx -o "$tmpdir/out.xlsx" -f
  run_and_check_nonempty "xlsx_to_csv" "$tmpdir/out.xlsx.csv" "$DTCONVERT" "$tmpdir/out.xlsx" --from xlsx --to csv -o "$tmpdir/out.xlsx.csv" -f
else
  skip_test "csv_to_xlsx + xlsx_to_csv" "missing xlsx2csv/libreoffice/ssconvert"
fi

# TXT -> PDF
if need_cmd enscript && need_cmd ps2pdf; then
  run_and_check_nonempty "txt_to_pdf" "$tmpdir/out.txt.pdf" "$DTCONVERT" "$tmpdir/in.txt" --to pdf -o "$tmpdir/out.txt.pdf" -f
else
  skip_test "txt_to_pdf" "missing enscript and/or ps2pdf"
fi

# CSV -> PDF
if need_cmd enscript && need_cmd ps2pdf; then
  run_and_check_nonempty "csv_to_pdf" "$tmpdir/out.csv.pdf" "$DTCONVERT" "$tmpdir/in.csv" --to pdf -o "$tmpdir/out.csv.pdf" -f
else
  skip_test "csv_to_pdf" "missing enscript and/or ps2pdf"
fi

# DOCX/ODT -> PDF
if need_cmd libreoffice; then
  if need_cmd pandoc; then
    printf '# Test Document\n\nHello from dtconvert.\n' > "$tmpdir/doc.md"
    pandoc "$tmpdir/doc.md" -o "$tmpdir/in.docx"
    pandoc "$tmpdir/doc.md" -o "$tmpdir/in.odt"
  else
    # Try generating docx/odt via LibreOffice from a simple text file.
    printf 'DTConvert test document\n\nHello from dtconvert.\n' > "$tmpdir/doc.txt"
    if libreoffice --headless --nologo --nolockcheck --nodefault --norestore --convert-to docx --outdir "$tmpdir" "$tmpdir/doc.txt" >/dev/null 2>&1; then
      :
    fi
    if libreoffice --headless --nologo --nolockcheck --nodefault --norestore --convert-to odt --outdir "$tmpdir" "$tmpdir/doc.txt" >/dev/null 2>&1; then
      :
    fi
    [[ -f "$tmpdir/doc.docx" ]] && mv "$tmpdir/doc.docx" "$tmpdir/in.docx" || true
    [[ -f "$tmpdir/doc.odt" ]] && mv "$tmpdir/doc.odt" "$tmpdir/in.odt" || true
  fi

  if [[ -f "$tmpdir/in.docx" ]]; then
    run_and_check_nonempty "docx_to_odt" "$tmpdir/out.docx.odt" "$DTCONVERT" "$tmpdir/in.docx" --to odt -o "$tmpdir/out.docx.odt" -f
    run_and_check_nonempty "docx_to_pdf" "$tmpdir/out.docx.pdf" "$DTCONVERT" "$tmpdir/in.docx" --to pdf -o "$tmpdir/out.docx.pdf" -f
  else
    skip_test "docx_to_pdf" "could not generate a DOCX input (pandoc missing; libreoffice conversion unavailable)"
  fi

  # If we couldn't generate an ODT directly, reuse the DOCX->ODT output.
  if [[ ! -f "$tmpdir/in.odt" && -f "$tmpdir/out.docx.odt" ]]; then
    cp "$tmpdir/out.docx.odt" "$tmpdir/in.odt"
  fi

  if [[ -f "$tmpdir/in.odt" ]]; then
    run_and_check_nonempty "odt_to_docx" "$tmpdir/out.odt.docx" "$DTCONVERT" "$tmpdir/in.odt" --to docx -o "$tmpdir/out.odt.docx" -f
    run_and_check_nonempty "odt_to_pdf" "$tmpdir/out.odt.pdf" "$DTCONVERT" "$tmpdir/in.odt" --to pdf -o "$tmpdir/out.odt.pdf" -f
  else
    skip_test "odt_to_pdf" "could not generate an ODT input (pandoc missing; libreoffice conversion unavailable)"
  fi
else
  skip_test "docx_to_pdf + odt_to_pdf" "missing libreoffice (and pandoc/unoconv)"
fi

# PostgreSQL import/export (optional, environment-dependent)
EXAMPLE_CFG="$ROOT_DIR/examples/postgresql.csv_to_postgresql.json"
if need_cmd psql && [[ -f "$EXAMPLE_CFG" ]]; then
  conn="$(sed -nE 's/^[[:space:]]*"connection"[[:space:]]*:[[:space:]]*"([^"]+)".*/\1/p' "$EXAMPLE_CFG" | head -n 1)"
  if [[ -n "$conn" ]] && psql -X -w "$conn" -c 'SELECT 1' >/dev/null 2>&1; then
    table="people_smoke_$$"
    cfg="$tmpdir/pg.json"
    cat >"$cfg" <<JSON
{
  "connection": "$conn",
  "table": "$table",
  "schema": "public",
  "create_table": true,
  "truncate": true
}
JSON
    run "csv_to_postgresql" "$DTCONVERT" "$tmpdir/in.csv" --to postgresql -o "$cfg"
    run_and_check_nonempty "postgresql_to_csv" "$tmpdir/out.pg.csv" "$DTCONVERT" "$cfg" --from postgresql --to csv -o "$tmpdir/out.pg.csv" -f
  else
    if [[ -z "${PGPASSWORD:-}" && ! -f "${HOME:-}/.pgpass" ]]; then
      skip_test "csv_to_postgresql + postgresql_to_csv" "psql not reachable/authenticated for example connection (export PGPASSWORD=... or create ~/.pgpass)"
    else
      skip_test "csv_to_postgresql + postgresql_to_csv" "psql not reachable/authenticated for example connection (PGPASSWORD/.pgpass present; check connection/user/db/host)"
    fi
  fi
elif ! need_cmd psql; then
  skip_test "csv_to_postgresql + postgresql_to_csv" "missing psql"
else
  skip_test "csv_to_postgresql + postgresql_to_csv" "missing examples/postgresql.csv_to_postgresql.json"
fi

echo
printf '== Summary ==\nPASS=%d FAIL=%d SKIP=%d\n' "$pass" "$fail" "$skip"

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
