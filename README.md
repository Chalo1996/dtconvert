# dtconvert

`dtconvert` is a Linux-first CLI for converting documents and data between common formats **and** for moving data in/out of PostgreSQL, with an optional AI helper for summarization, web search, and citations.

The core binary is written in C for speed, portability, and predictable behavior. Actual file-format conversions are delegated to small converter modules (shell scripts) so it’s easy to extend.

## What problem this solves

If you routinely deal with:

- Mixed formats (DOCX/ODT/TXT/PDF/CSV/XLSX/JSON/YAML)
- “Just convert this quickly” tasks that are annoying to do repeatedly
- Moving CSV data into PostgreSQL and exporting results back to CSV
- Needing quick, local-first summarization/citations over documents and URLs

…`dtconvert` provides a single CLI interface with consistent flags, clear errors, and a modular conversion pipeline.

## Key goals

- **Simple UX**: `dtconvert <input> --to <format>`
- **Linux-first**: shells out to common tools, designed for scripting
- **Modular conversion**: add/replace converters without changing the core binary
- **Local-first AI**: works with Ollama locally; also supports an OpenAI-compatible backend
- **Predictable behavior**: stable exit codes and minimal surprises

## Uniqueness / prior art

There are plenty of existing tools that cover parts of this space (document converters like Pandoc/LibreOffice, data-format converters, `psql`-based import/export scripts, and standalone AI CLI tools).

`dtconvert` is intended to be distinctive in how it packages these workflows:

- **One CLI for common “format glue” work**: document/data conversion + PostgreSQL import/export + optional AI helper.
- **Easy to extend**: most new conversions can be added as a small script in `modules/`.
- **Simple pipelines**: some conversions intentionally chain steps (e.g., CSV→PDF via CSV→TXT→PDF).
- **Clear dependencies**: install guidance is mapped to modules/features so you can keep setups minimal.

## Quick start

### 1) Build

```bash
make
```

This builds:

- `bin/dtconvert` (the main CLI)
- helper binaries under `lib/converters/` used by some modules

### 2) Run

```bash
./bin/dtconvert --help
./bin/dtconvert --version
```

### 3) Install

System-wide install (requires sudo):

```bash
sudo make install
```

User install (no sudo):

```bash
make install PREFIX=$HOME/.local
```

Notes:

- Converter modules and helper binaries are installed under `$PREFIX/lib/dtconvert/`.
- If `$PREFIX/bin` is not on your PATH, `make install` appends an `export PATH=...` line to your shell rc files (idempotent) and asks you to restart your shell.

## Dependencies

`dtconvert` builds with just a C toolchain, but some conversions rely on external tools.

- Build (required): `gcc` (or `clang`) and `make`
- AI (`dtconvert ai ...`): `curl` (required), `xdg-open` (only for `ai search --open`)
- DOCX/ODT→PDF: `libreoffice` (recommended) or `unoconv` or `pandoc` (pandoc PDF output may require LaTeX)
- TXT/CSV→PDF: `enscript` + Ghostscript (`ps2pdf`)
- XLSX/CSV: `xlsx2csv` (preferred) or `libreoffice` or `ssconvert` (Gnumeric)
- PostgreSQL: `psql` (`postgresql-client`)

Ubuntu/Debian (install only what you need):

```bash
sudo apt update
sudo apt install -y build-essential \
  curl xdg-utils \
  libreoffice unoconv pandoc texlive-latex-recommended \
  enscript ghostscript \
  xlsx2csv gnumeric \
  postgresql-client
```

Fedora (install only what you need):

```bash
sudo dnf install -y gcc make \
  curl xdg-utils \
  libreoffice unoconv pandoc \
  texlive-scheme-medium \
  enscript ghostscript \
  xlsx2csv gnumeric \
  postgresql
```

Arch Linux (install only what you need):

```bash
sudo pacman -S --needed base-devel \
  curl xdg-utils \
  libreoffice-fresh unoconv pandoc \
  texlive \
  enscript ghostscript \
  xlsx2csv gnumeric \
  postgresql
```

Note (Arch): If you prefer the stable LibreOffice track, replace `libreoffice-fresh` with `libreoffice-still`. If a package isn’t available in your enabled repos (varies by distro mirrors/config), you may need an AUR build instead.

## Usage

### Convert files

```bash
./bin/dtconvert document.docx --to pdf
./bin/dtconvert notes.txt --to pdf -o notes.pdf
./bin/dtconvert spreadsheet.xlsx --to csv
./bin/dtconvert data.csv --to json
./bin/dtconvert data.yaml --to json
```

### PostgreSQL import/export

Import a CSV into PostgreSQL using a JSON config file:

```bash
./bin/dtconvert people.csv --to postgresql -o examples/postgresql.csv_to_postgresql.json
```

Export from PostgreSQL to CSV:

```bash
./bin/dtconvert examples/postgresql.csv_to_postgresql.json --from postgresql --to csv -o export.csv
```

Notes:

- PostgreSQL operations require `psql` available on your PATH.
- Import/export uses a JSON config file passed via `-o/--output`.
- Import/export will not prompt for a password; set up credentials via `.pgpass` or `PGPASSWORD` rather than embedding passwords in the config file.

### AI helper

AI features are built into the `dtconvert` binary:

- `summarize`: summarize a local file
- `search`: print a DuckDuckGo URL and optionally open it in your browser
- `cite`: generate simple APA/MLA citations for one or more URLs

Examples:

```bash
./bin/dtconvert ai summarize README.md
./bin/dtconvert ai search "postgresql copy csv" --open
./bin/dtconvert ai cite https://example.com --style apa
```

AI dependencies:

- `curl` (HTTP requests)
- `xdg-open` (only needed when using `ai search --open`)

#### AI environment variables

General:

- `DTCONVERT_AI_TIMEOUT` (curl max-time in seconds; affects `ai summarize` and `ai cite`. Defaults vary by operation.)

Local-first (Ollama):

- `DTCONVERT_OLLAMA_HOST` (default: `http://127.0.0.1:11434`)
- `DTCONVERT_OLLAMA_MODEL` (default: `llama3.1`)

OpenAI-compatible:

- `OPENAI_API_KEY` (required)
- `OPENAI_BASE_URL` (optional, default: `https://api.openai.com/v1`)
- `DTCONVERT_OPENAI_MODEL` (default: `gpt-4o-mini`)

## How it works (high level)

- The main CLI (C) parses arguments, validates inputs, and selects a converter.
- Converters are executable scripts under `modules/` and follow a simple contract:

```text
<module_script> <input_path> <output_path>
```

- Some modules call small helper binaries built from C sources in `lib/converters/`.

For more details, see:

- [Architecture.md](Architecture.md)
- [Requirements.md](Requirements.md)

## Adding a new converter module

1. Create a new script in `modules/`, for example `modules/foo_to_bar.sh`.
2. Ensure it’s executable.
3. Register it in the converter registry (currently a static table in `src/conversion.c`).

## Project status

This is an MVP focused on pragmatic conversions and predictable CLI behavior. Expect formats and modules to evolve.

Contributions are welcome. The project currently has a single maintainer.

Maintainer: Emmanuel Chalo (<emusyoka759@gmail.com>)

## License

Not specified yet.
