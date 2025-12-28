# Requirements

This project is a Linux-first CLI written in C, plus a set of converter modules in `modules/`.
Some modules depend on external system tools.

## Ubuntu 24.04 (example setup)

### 1) Build dependencies (Ubuntu, required)

Install the compiler toolchain and build tools:

```bash
sudo apt update
sudo apt install -y build-essential
```

## Fedora (example setup)

### 1) Build dependencies (Fedora, required)

```bash
sudo dnf install -y gcc make
```

### 2) Optional runtime packages (Fedora, install only what you need)

```bash
sudo dnf install -y curl xdg-utils \
  libreoffice unoconv pandoc \
  texlive-scheme-medium \
  enscript ghostscript \
  xlsx2csv gnumeric \
  postgresql
```

## Arch Linux (example setup)

### 1) Build dependencies (Arch, required)

```bash
sudo pacman -S --needed base-devel
```

### 2) Optional runtime packages (Arch, install only what you need)

```bash
sudo pacman -S --needed curl xdg-utils \
  libreoffice-fresh unoconv pandoc \
  texlive \
  enscript ghostscript \
  xlsx2csv gnumeric \
  postgresql
```

Note (Arch): Replace `libreoffice-fresh` with `libreoffice-still` if you want the stable track. Depending on your configured repositories, some utilities may require AUR.

Build the project:

```bash
make
```

Quick sanity check:

```bash
./bin/dtconvert --version
```

## Install (optional)

System-wide (requires sudo):

```bash
sudo make install
```

User install (no sudo):

```bash
make install PREFIX=$HOME/.local
```

If `$PREFIX/bin` is not on your PATH, `make install` appends an `export PATH=...` line to your shell rc files and you’ll need to start a new shell session.

### 2) Runtime dependencies (core)

Core conversions like CSV↔JSON↔YAML, TXT→tokens, CSV↔SQL use built helper binaries under `lib/converters/`.
These are built by `make` and do not require extra system packages.

## Dependency checklist (by module)

This is the explicit dependency map for the current `modules/*.sh` implementations:

- CSV→TXT (`modules/csv_to_txt.sh`): `column` (optional; prettier output), otherwise uses `sed`
- TXT→PDF (`modules/txt_to_pdf.sh`): `enscript` + Ghostscript (`ps2pdf`)
- CSV→PDF (`modules/csv_to_pdf.sh`): uses CSV→TXT + TXT→PDF
- DOCX→PDF (`modules/docx_to_pdf.sh`): `libreoffice` (preferred) or `unoconv` or `pandoc`
- DOCX→ODT (`modules/docx_to_odt.sh`): `libreoffice` or `unoconv`
- ODT→PDF (`modules/odt_to_pdf.sh`): `libreoffice` or `unoconv`
- ODT→DOCX (`modules/odt_to_docx.sh`): `libreoffice` or `unoconv`
- CSV↔XLSX (`modules/csv_to_xlsx.sh`, `modules/xlsx_to_csv.sh`): `xlsx2csv` or `libreoffice` or `ssconvert`
- PostgreSQL (`modules/csv_to_postgresql.sh`, `modules/postgresql_to_csv.sh`): `psql`
- AI (`dtconvert ai ...`): `curl`; plus `xdg-open` only when opening a browser

Quick verify (optional):

```bash
command -v curl libreoffice unoconv pandoc enscript ps2pdf xlsx2csv ssconvert psql xdg-open column || true
```

## Optional dependencies (by feature)

Install only the tools needed for the conversions you plan to use.

### PDF output

#### CSV → PDF (`modules/csv_to_pdf.sh`)

Uses the existing `CSV → TXT` and `TXT → PDF` modules.

- Required: same packages as `TXT → PDF` (`enscript` + `ghostscript`)
- Optional: `column` (usually already present via `util-linux`) for nicer text layout

#### TXT → PDF (`modules/txt_to_pdf.sh`)

Requires `enscript` and Ghostscript (`ps2pdf`).

```bash
sudo apt update
sudo apt install -y enscript ghostscript
```

Verify:

```bash
command -v enscript
command -v ps2pdf
```

#### DOCX → PDF (`modules/docx_to_pdf.sh`)

Prefers LibreOffice; falls back to `unoconv` or `pandoc`.

Recommended:

```bash
sudo apt update
sudo apt install -y libreoffice
```

Note: When using LibreOffice in headless mode, you may see `Warning: failed to launch javaldx - java may not function correctly`.
This usually does not affect DOCX/ODT → PDF conversions; it just means Java-dependent LibreOffice features may not work.
If you want to suppress the warning, install a JRE and LibreOffice Java support (Ubuntu/Debian: `sudo apt install -y default-jre libreoffice-java-common`).

Alternatives:

```bash
sudo apt install -y unoconv
# or
sudo apt install -y pandoc
```

Note: `pandoc` PDF output may require a LaTeX engine; if you use pandoc-to-PDF, you may need:

```bash
sudo apt install -y texlive-latex-recommended
```

#### ODT → PDF (`modules/odt_to_pdf.sh`)

Requires LibreOffice or `unoconv`:

```bash
sudo apt update
sudo apt install -y libreoffice
# or
sudo apt install -y unoconv
```

Note: If you see `Warning: failed to launch javaldx - java may not function correctly`, install `default-jre` and `libreoffice-java-common` on Ubuntu/Debian.

If the warning persists, verify that `javaldx` exists on your system:

```bash
find / -name javaldx 2>/dev/null
```

If it is not found, install LibreOffice's Java runtime environment package (Debian/Ubuntu):

```bash
sudo apt install -y ure-java
find / -name javaldx 2>/dev/null
# commonly: /usr/lib/libreoffice/program/javaldx
```

### Excel / XLSX

#### CSV → XLSX (`modules/csv_to_xlsx.sh`)

Requires LibreOffice or `ssconvert` (Gnumeric):

```bash
sudo apt update
sudo apt install -y libreoffice
# or
sudo apt install -y gnumeric
```

#### XLSX → CSV (`modules/xlsx_to_csv.sh`)

Prefers `xlsx2csv`, otherwise LibreOffice or `ssconvert`:

```bash
sudo apt update
sudo apt install -y xlsx2csv
# or
sudo apt install -y libreoffice
# or
sudo apt install -y gnumeric
```

Verify:

```bash
command -v xlsx2csv || true
command -v libreoffice || true
command -v ssconvert || true
```

### PostgreSQL import/export

DB conversions use `lib/converters/pg_store` (C helper) and shell out to `psql`.

Credential note: prefer using `~/.pgpass` or setting `PGPASSWORD` for passwords instead of embedding passwords in the JSON `connection` string.

Install PostgreSQL client tools:

```bash
sudo apt update
sudo apt install -y postgresql-client
```

Verify:

```bash
command -v psql
```

If you want a local PostgreSQL server as well:

```bash
sudo apt install -y postgresql
```

#### Local PostgreSQL setup (example)

Create a local user/database that matches the default example config:

```bash
sudo -u postgres psql -c "CREATE USER dtconvert WITH PASSWORD 'dtconvert';"
sudo -u postgres psql -c "CREATE DATABASE dtconvertdb OWNER dtconvert;"
```

Verify connectivity:

```bash
PGPASSWORD=dtconvert psql -X -w -h localhost -U dtconvert -d dtconvertdb -c 'SELECT current_user, current_database();'
```

#### Running the full conversion suite with PostgreSQL

Because `dtconvert` uses non-interactive `psql` under the hood, it will not prompt for a password.
To include PostgreSQL in the thorough conversion sweep:

```bash
PGPASSWORD=dtconvert make conversions-smoke
```

Recommended for repeatable local use (no env var):

```bash
printf '%s\n' 'localhost:5432:dtconvertdb:dtconvert:dtconvert' >> ~/.pgpass
chmod 600 ~/.pgpass
```

### AI features (`dtconvert ai ...`)

The AI subcommands are optional and use external tools:

- `curl` (HTTP requests)
- `xdg-open` (only needed for `ai search --open`)

```bash
sudo apt update
sudo apt install -y curl xdg-utils
```

#### Local (default): Ollama

- Requires an Ollama server running.
- Environment variables (preferred):
  - `DTCONVERT_OLLAMA_HOST` (default `http://127.0.0.1:11434`)
  - `DTCONVERT_OLLAMA_MODEL` (default `llama3.1`)

#### Cloud: OpenAI-compatible

- Requires `OPENAI_API_KEY`.
- Optional:
  - `OPENAI_BASE_URL` (default `https://api.openai.com/v1`)
  - `DTCONVERT_OPENAI_MODEL` (default `gpt-4o-mini`)

#### AI examples

Search (prints URL; prompts before opening unless `--yes`):

```bash
./bin/dtconvert ai search "postgresql copy csv" --open
./bin/dtconvert ai search postgres copy csv --open --yes
```

Summarize a local file (Ollama default):

```bash
./bin/dtconvert ai summarize Requirements.md
./bin/dtconvert ai summarize Requirements.md -o summary.md
```

Summarize using OpenAI-compatible backend:

```bash
export OPENAI_API_KEY="..."
./bin/dtconvert ai summarize Requirements.md --backend openai
```

Cite one or more URLs (APA/MLA):

```bash
./bin/dtconvert ai cite https://example.com --style apa
./bin/dtconvert ai cite https://example.com https://duckduckgo.com --style mla -o citations.txt
```

## Notes

- Some basic utilities are typically already present on Ubuntu (e.g., `sed`).
- The CSV→TXT module (`modules/csv_to_txt.sh`) will use `column` if present; on Ubuntu it is provided by `util-linux` (usually installed by default).

## Troubleshooting

- If a conversion fails with “converter not found”, ensure the target format is supported and the module exists in `modules/`.
- If a conversion fails with “helper not found”, run `make` to build helper binaries under `lib/converters/`.
- For PDF modules, missing tools will typically produce a message like “Install enscript and ghostscript”.
