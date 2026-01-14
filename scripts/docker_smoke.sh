#!/usr/bin/env bash
# =============================================================================
# Docker Smoke Test Script
# Validates the dtconvert Docker image meets all requirements
# Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 2.2, 4.1, 4.2
# =============================================================================
set -euo pipefail

IMAGE_NAME="${DTCONVERT_IMAGE:-dtconvert:test}"

echo "== Docker Smoke Tests for dtconvert =="
echo "Image: $IMAGE_NAME"
echo

pass=0
fail=0

# Helper function to run a test
run_test() {
  local name="$1"; shift
  printf '%-50s ' "$name"
  if "$@" >/dev/null 2>&1; then
    echo "PASS"
    pass=$((pass+1))
    return 0
  else
    echo "FAIL"
    fail=$((fail+1))
    return 1
  fi
}

# Helper function to check tool presence in container
check_tool_present() {
  local tool="$1"
  docker run --rm --entrypoint which "$IMAGE_NAME" "$tool"
}

# Helper function to check tool absence in container
check_tool_absent() {
  local tool="$1"
  ! docker run --rm --entrypoint which "$IMAGE_NAME" "$tool" 2>/dev/null
}

# =============================================================================
# Test 1: Tool Presence (Requirements 1.1, 1.2, 1.3, 1.4, 1.5)
# =============================================================================
echo "== Tool Presence Tests =="

# Requirement 1.1: LibreOffice for DOCX/ODT conversions
run_test "LibreOffice present (Req 1.1)" check_tool_present libreoffice

# Requirement 1.2: enscript for PDF generation
run_test "enscript present (Req 1.2)" check_tool_present enscript

# Requirement 1.2: Ghostscript (ps2pdf) for PDF generation
run_test "ps2pdf present (Req 1.2)" check_tool_present ps2pdf

# Requirement 1.3: xlsx2csv for Excel conversions
run_test "xlsx2csv present (Req 1.3)" check_tool_present xlsx2csv

# Requirement 1.4: PostgreSQL client tools
run_test "psql present (Req 1.4)" check_tool_present psql

# Requirement 1.5: curl for AI features
run_test "curl present (Req 1.5)" check_tool_present curl

# Requirement 1.6: dtconvert binary
run_test "dtconvert binary present (Req 1.6)" check_tool_present dtconvert

echo

# =============================================================================
# Test 2: Build Tool Absence (Requirement 2.2)
# =============================================================================
echo "== Build Tool Absence Tests =="

# Requirement 2.2: gcc should NOT be in final image
run_test "gcc absent (Req 2.2)" check_tool_absent gcc

# Requirement 2.2: make should NOT be in final image
run_test "make absent (Req 2.2)" check_tool_absent make

echo

# =============================================================================
# Test 3: Entrypoint Functionality (Requirements 4.1, 4.2)
# =============================================================================
echo "== Entrypoint Tests =="

# Requirement 4.1, 4.2: dtconvert is entrypoint and passes arguments
run_test "Entrypoint --version works (Req 4.1, 4.2)" \
  docker run --rm "$IMAGE_NAME" --version

run_test "Entrypoint --help works (Req 4.1, 4.2)" \
  docker run --rm "$IMAGE_NAME" --help

echo

# =============================================================================
# Test 4: Basic Conversion Test
# =============================================================================
echo "== Basic Conversion Tests =="

# Create temporary directory for test files
tmpdir="$(mktemp -d /tmp/dtconvert_docker_smoke.XXXXXX)"
trap 'rm -rf "$tmpdir"' EXIT

# Create test input file
cat >"$tmpdir/test.csv" <<'CSV'
name,age,city
Alice,30,Seattle
Bob,25,Portland
CSV

# Test CSV to JSON conversion
run_test "CSV to JSON conversion" \
  docker run --rm -v "$tmpdir:/data" "$IMAGE_NAME" /data/test.csv --to json -o /data/test.json -f

# Verify output file exists and is non-empty
if [[ -f "$tmpdir/test.json" && -s "$tmpdir/test.json" ]]; then
  run_test "JSON output file created and non-empty" true
else
  run_test "JSON output file created and non-empty" false
fi

# Test CSV to TXT conversion
run_test "CSV to TXT conversion" \
  docker run --rm -v "$tmpdir:/data" "$IMAGE_NAME" /data/test.csv --to txt -o /data/test.txt -f

# Verify output file exists and is non-empty
if [[ -f "$tmpdir/test.txt" && -s "$tmpdir/test.txt" ]]; then
  run_test "TXT output file created and non-empty" true
else
  run_test "TXT output file created and non-empty" false
fi

echo

# =============================================================================
# Test 5: Environment Variable Support
# =============================================================================
echo "== Environment Variable Tests =="

# Test DTCONVERT_HOME is set correctly
run_test "DTCONVERT_HOME environment variable set" \
  docker run --rm --entrypoint sh "$IMAGE_NAME" -c 'test -n "$DTCONVERT_HOME"'

# Test converters directory exists
run_test "Converters directory exists" \
  docker run --rm --entrypoint sh "$IMAGE_NAME" -c 'test -d "$DTCONVERT_HOME/converters"'

# Test lib/converters directory exists
run_test "lib/converters directory exists" \
  docker run --rm --entrypoint sh "$IMAGE_NAME" -c 'test -d "$DTCONVERT_HOME/lib/converters"'

echo

# =============================================================================
# Summary
# =============================================================================
echo "== Summary =="
printf 'PASS=%d FAIL=%d\n' "$pass" "$fail"

if [[ "$fail" -ne 0 ]]; then
  echo
  echo "Some tests failed. Please check the Docker image configuration."
  exit 1
fi

echo
echo "All Docker smoke tests passed!"
exit 0
