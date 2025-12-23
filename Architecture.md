# dtconvert Architecture

## Architecture Overview

`dtconvert` is a Linux-first CLI that keeps the “core” logic in C and delegates actual format conversions to external modules (currently shell scripts). This keeps the binary small and makes adding new converters simple.

### High-level flow

1. CLI parses intent: `dtconvert <input> --to <format> [options]`.

2. Input document is opened/validated and its extension is normalized.
3. A converter is selected based on `(input_extension, output_format)` unless `--from` overrides the detected input format.
4. The converter module is executed as a separate process.
5. The CLI returns a stable exit code for scripting.

### Code layout

```text
dtconvert/
├── include/
│   └── dtconvert.h             # Public structs and function prototypes
├── src/
│   ├── main.c                  # Program entry point and high-level orchestration
│   ├── ai.c                    # AI subcommands (summarize/search/cite)
│   ├── utils.c                 # CLI parsing and shared utility functions
│   ├── document.c              # Document path, extension parsing, and validation
│   ├── conversion.c            # Converter selection and module execution (fork/exec)
│   └── formats.c               # Supported formats and format metadata
├── modules/
│   ├── docx_to_pdf.sh          # Format-specific conversion modules (shell scripts)
│   ├── odt_to_pdf.sh
│   ├── txt_to_pdf.sh
│   ├── csv_to_txt.sh
│   ├── csv_to_xlsx.sh
│   ├── xlsx_to_csv.sh
│   ├── csv_to_json.sh
│   ├── json_to_csv.sh
│   ├── csv_to_sql.sh
│   ├── sql_to_csv.sh
│   ├── txt_to_tokens.sh
│   ├── csv_to_postgresql.sh
│   ├── postgresql_to_csv.sh
│
├── lib/
│   └── converters/             # Small helper binaries used by modules
│       ├── data_convert.c      # Builds: lib/converters/data_convert
│       ├── sql_convert.c       # Builds: lib/converters/sql_convert
│       ├── pg_store.c          # Builds: lib/converters/pg_store
│       ├── tokenize.c          # Builds: lib/converters/tokenize
│       └── (sources only)
├── bin/                         # Compiled binaries
├── obj/                         # Build artifacts and intermediate objects
├── Makefile                    # Build configuration
└── build.sh                    # Convenience build script
```

### Module contract (converter scripts)

Converter modules are executed like:

`<converter_script> <input_path> <output_path>`

Rules:

- Exit code `0` means success; non-zero means failure.
- The script should create the output file at `output_path`.

Built-in helper binaries:

- `lib/converters/data_convert` is a small C helper used for `csv/json/yaml` conversions.
- `lib/converters/sql_convert` is a small C helper used for `csv/sql` conversions.
- `lib/converters/pg_store` is a small C helper used for PostgreSQL import/export by shelling out to `psql`.
- YAML support is intentionally a small, predictable subset (list of mappings). It is designed for interchange with this tool, not arbitrary YAML documents.

Special case (storage targets):

- For targets like `postgresql`, the second argument is a config file path (passed via `-o`), not an output file path.

Special case (storage sources):

- For sources like `postgresql` (export), the input document is often a JSON config file; use `--from postgresql` so the converter is selected correctly.
- Export config supports either `table`/`schema` or a raw `query` string (written to CSV with a header).

### AI commands (`dtconvert ai ...`)

AI features are implemented in-process (compiled into the main `dtconvert` binary) rather than as converter modules.

- Implementation: `src/ai.c` (dispatched early from `src/main.c`)
- Transport: shells out to `curl` for HTTP requests
- Browser open: uses `xdg-open` only when `ai search --open` is requested

This keeps AI features independent from the conversion module contract and avoids treating AI actions as file-format converters.

### Adding a new converter (MVP)

The converter registry is currently a static table in `src/conversion.c`.

To add `xlsx -> csv`:

1. Create `modules/xlsx_to_csv.sh` and make it executable.
2. Add a new entry to the converter registry: `{ "xlsx", "csv", "modules/xlsx_to_csv.sh", "XLSX to CSV converter" }`.

### Planned next steps

- Auto-discover converters from `modules/`/`lib/converters/` instead of a static table.
- Standardize module metadata (e.g., `--from`, `--to`, `--describe`).
- Enhance multi-step pipelines (e.g., `docx -> pdf -> txt`) via the conversion graph.
- Add storage backends as modules (e.g., `--to postgresql/mysql/mongo`) once a stable way to pass connection config is defined (env vars vs `-o` config file).
