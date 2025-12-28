# Makefile for dtconvert
CC = gcc

# Install prefix (override with e.g. `make install PREFIX=$$HOME/.local`)
PREFIX ?= /usr/local
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin
DTCONVERT_LIBDIR ?= $(PREFIX)/lib/dtconvert
MODULES_INSTALL_DIR ?= $(DTCONVERT_LIBDIR)/converters
HELPERS_INSTALL_DIR ?= $(DTCONVERT_LIBDIR)/lib/converters
INSTALL ?= install

# Build flags
# - OPT_CFLAGS: optimization + debug info (override: `make OPT_CFLAGS='-O0 -g3'`)
# - HARDEN_CFLAGS/LDFLAGS: basic security hardening (override to disable if needed)
BASE_CFLAGS = -Wall -Wextra -Wformat=2 -Wformat-security -I./include
OPT_CFLAGS ?= -O2 -g
HARDEN_CFLAGS ?= -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
HARDEN_LDFLAGS ?= -pie -Wl,-z,relro -Wl,-z,now

CFLAGS = $(BASE_CFLAGS) $(OPT_CFLAGS) $(HARDEN_CFLAGS)
LDFLAGS = $(HARDEN_LDFLAGS)
TARGET = dtconvert
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
MODULES_DIR = modules
LIB_DIR = lib

DATA_CONVERT = $(LIB_DIR)/converters/data_convert
DATA_CONVERT_SRC = $(LIB_DIR)/converters/data_convert.c

TOKENIZE = $(LIB_DIR)/converters/tokenize
TOKENIZE_SRC = $(LIB_DIR)/converters/tokenize.c

SQL_CONVERT = $(LIB_DIR)/converters/sql_convert
SQL_CONVERT_SRC = $(LIB_DIR)/converters/sql_convert.c

PG_STORE = $(LIB_DIR)/converters/pg_store
PG_STORE_SRC = $(LIB_DIR)/converters/pg_store.c

# Source files - explicitly list all of them
SRCS = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/ai.c \
	$(SRC_DIR)/document.c \
	$(SRC_DIR)/conversion.c \
	$(SRC_DIR)/utils.c \
	$(SRC_DIR)/formats.c

OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Default target
all: directories $(BIN_DIR)/$(TARGET) $(DATA_CONVERT) $(TOKENIZE) $(SQL_CONVERT) $(PG_STORE) modules

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) $(MODULES_DIR) $(LIB_DIR)/converters

# Link the executable
$(BIN_DIR)/$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# Build helper converter binaries
$(DATA_CONVERT): $(DATA_CONVERT_SRC)
	$(CC) $(CFLAGS) $< -o $@
	@chmod +x $@

$(TOKENIZE): $(TOKENIZE_SRC)
	$(CC) $(CFLAGS) $< -o $@
	@chmod +x $@

$(SQL_CONVERT): $(SQL_CONVERT_SRC)
	$(CC) $(CFLAGS) $< -o $@
	@chmod +x $@

$(PG_STORE): $(PG_STORE_SRC)
	$(CC) $(CFLAGS) $< -o $@
	@chmod +x $@

# Compile C files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Make converter modules executable
modules:
	@chmod +x $(MODULES_DIR)/*.sh 2>/dev/null || true
	@echo "Made converter scripts executable"

# Create sample converter scripts if they don't exist
samples:
	@if [ ! -f $(MODULES_DIR)/docx_to_pdf.sh ]; then \
		echo "Creating sample converter scripts..."; \
		echo '#!/bin/bash' > $(MODULES_DIR)/docx_to_pdf.sh; \
		echo 'echo "Sample converter: $$1 -> $$2"' >> $(MODULES_DIR)/docx_to_pdf.sh; \
		echo 'touch $$2' >> $(MODULES_DIR)/docx_to_pdf.sh; \
		chmod +x $(MODULES_DIR)/docx_to_pdf.sh; \
	fi
	@if [ ! -f $(MODULES_DIR)/odt_to_pdf.sh ]; then \
		echo '#!/bin/bash' > $(MODULES_DIR)/odt_to_pdf.sh; \
		echo 'echo "Sample converter: $$1 -> $$2"' >> $(MODULES_DIR)/odt_to_pdf.sh; \
		echo 'touch $$2' >> $(MODULES_DIR)/odt_to_pdf.sh; \
		chmod +x $(MODULES_DIR)/odt_to_pdf.sh; \
	fi

# Install system-wide
install: all
	@echo "Installing to $(DESTDIR)$(BINDIR)..."
	@$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	@$(INSTALL) -d "$(DESTDIR)$(MODULES_INSTALL_DIR)"
	@$(INSTALL) -d "$(DESTDIR)$(HELPERS_INSTALL_DIR)"
	@$(INSTALL) -m 0755 $(BIN_DIR)/$(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	@$(INSTALL) -m 0755 $(MODULES_DIR)/*.sh "$(DESTDIR)$(MODULES_INSTALL_DIR)/"
	@$(INSTALL) -m 0755 $(DATA_CONVERT) $(TOKENIZE) $(SQL_CONVERT) $(PG_STORE) "$(DESTDIR)$(HELPERS_INSTALL_DIR)/"
	@# If installing to a user prefix, ensure the installed bin dir is on PATH.
	@if [ -z "$(DESTDIR)" ]; then \
		if [ ! -w "$(BINDIR)" ] 2>/dev/null; then :; fi; \
		if ! echo "$$PATH" | tr ':' '\n' | grep -Fxq "$(BINDIR)"; then \
			echo "NOTE: $(BINDIR) is not on your PATH."; \
			if [ "$(BINDIR)" = "$$HOME/.local/bin" ]; then \
				exportline='export PATH="$$HOME/.local/bin:$$PATH"'; \
			else \
				exportline='export PATH="$(BINDIR):$$PATH"'; \
			fi; \
			for rc in "$$HOME/.profile" "$$HOME/.bashrc" "$$HOME/.zshrc"; do \
				if [ -f "$$rc" ] || [ "$$rc" = "$$HOME/.profile" ]; then \
					if ! grep -Fq "# dtconvert PATH" "$$rc" 2>/dev/null; then \
						printf '\n# dtconvert PATH\n%s\n' "$$exportline" >> "$$rc"; \
						echo "Added PATH export to $$rc"; \
					fi; \
				fi; \
			done; \
			echo "Restart your shell (or source your rc file) to use 'dtconvert' directly."; \
		fi; \
	fi
	@echo "Installation complete"

# Uninstall
uninstall:
	@echo "Uninstalling..."
	@rm -f "$(DESTDIR)$(BINDIR)/$(TARGET)"
	@rm -rf "$(DESTDIR)$(DTCONVERT_LIBDIR)"
	@echo "Uninstallation complete"

# Clean build files
clean:
	@rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Cleaned build files"

# Run tests
test: all samples
	@echo "Running basic tests..."
	@./$(BIN_DIR)/$(TARGET) --version
	@echo ""
	@echo "Test completed"

# Repeatable smoke tests (no external services required)
smoke: all
	@echo "Running smoke tests..."
	@set -eu; \
		tmpdir=$$(mktemp -d /tmp/dtconvert_smoke.XXXXXX); \
		trap 'rm -rf "$$tmpdir"' EXIT; \
		printf 'name,age\nAlice,30\nBob,41\n' > "$$tmpdir/in.csv"; \
		printf 'hello world\nthis is a test\n' > "$$tmpdir/in.txt"; \
		./$(BIN_DIR)/$(TARGET) "$$tmpdir/in.csv" --to json -o "$$tmpdir/out.json"; \
		./$(BIN_DIR)/$(TARGET) "$$tmpdir/out.json" --from json --to csv -o "$$tmpdir/out.csv"; \
		./$(BIN_DIR)/$(TARGET) "$$tmpdir/in.txt" --to tokens -o "$$tmpdir/out.tokens"; \
		head -n 3 "$$tmpdir/out.csv"; \
		head -n 10 "$$tmpdir/out.tokens"; \
		echo "Smoke completed"

# Thorough conversion sweep across all registered converters.
# Skips only those that require missing external tools or services.
conversions-smoke: all
	@chmod +x scripts/conversions_smoke.sh
	@./scripts/conversions_smoke.sh

update-supported-conversions:
	@chmod +x scripts/update_supported_conversions.sh
	@./scripts/update_supported_conversions.sh

# Repeatable AI smoke tests
# - Always: ai search, ai cite (offline via file://)
# - Optional: ai summarize (ollama) if local Ollama is reachable
# - Optional: ai summarize (openai) if OPENAI_API_KEY is set
ai-smoke: all
	@echo "Running AI smoke tests..."
	@set -eu; \
		tmpdir=$$(mktemp -d /tmp/dtconvert_ai_smoke.XXXXXX); \
		trap 'rm -rf "$$tmpdir"' EXIT; \
		printf '%s\n' \
			'<!doctype html>' \
			'<html>' \
			'<head>' \
			'  <title>DTConvert AI Cite Smoke</title>' \
			'  <meta name="author" content="Ada Lovelace">' \
			'  <meta name="date" content="2025-12-23">' \
			'  <meta property="og:site_name" content="Local Test Site">' \
			'</head>' \
			'<body>hello</body>' \
			'</html>' \
			> "$$tmpdir/page.html"; \
		printf 'dtconvert is a CLI tool. It converts files.\nIt has modules for PDF and PostgreSQL.\n' > "$$tmpdir/in.txt"; \
		./$(BIN_DIR)/$(TARGET) ai search postgresql copy csv | head -n 1; \
		./$(BIN_DIR)/$(TARGET) ai cite "file://$$tmpdir/page.html" --style apa | head -n 2; \
		if curl -sS --connect-timeout 1 --max-time 2 http://127.0.0.1:11434/api/tags >/dev/null 2>&1; then \
			echo "Ollama reachable; running ai summarize (ollama)..."; \
			DTCONVERT_AI_TIMEOUT=180 ./$(BIN_DIR)/$(TARGET) ai summarize "$$tmpdir/in.txt" --backend ollama --model "$${DTCONVERT_OLLAMA_MODEL:-llama3.1}" | head -n 20; \
		else \
			echo "Ollama not reachable; skipping ollama summarize."; \
		fi; \
		if [ -n "$${OPENAI_API_KEY:-}" ]; then \
			echo "OPENAI_API_KEY is set; running ai summarize (openai)..."; \
			set +e; \
			DTCONVERT_AI_TIMEOUT=60 ./$(BIN_DIR)/$(TARGET) ai summarize "$$tmpdir/in.txt" --backend openai --model "$${DTCONVERT_OPENAI_MODEL:-gpt-4o-mini}" >"$$tmpdir/openai.out" 2>"$$tmpdir/openai.err"; \
			rc=$$?; \
			set -e; \
			if [ $$rc -eq 0 ]; then \
				head -n 20 "$$tmpdir/openai.out"; \
			else \
				echo "OpenAI summarize failed (non-fatal): $$(head -n 1 "$$tmpdir/openai.err")"; \
			fi; \
		else \
			echo "OPENAI_API_KEY not set; skipping openai summarize."; \
		fi; \
		echo "AI smoke completed"

ai-smoke-strict: all
	@echo "Running AI smoke tests (strict)..."
	@set -eu; \
		tmpdir=$$(mktemp -d /tmp/dtconvert_ai_smoke.XXXXXX); \
		trap 'rm -rf "$$tmpdir"' EXIT; \
		printf '%s\n' \
			'<!doctype html>' \
			'<html>' \
			'<head>' \
			'  <title>DTConvert AI Cite Smoke</title>' \
			'  <meta name="author" content="Ada Lovelace">' \
			'  <meta name="date" content="2025-12-23">' \
			'  <meta property="og:site_name" content="Local Test Site">' \
			'</head>' \
			'<body>hello</body>' \
			'</html>' \
			> "$$tmpdir/page.html"; \
		printf 'dtconvert is a CLI tool. It converts files.\nIt has modules for PDF and PostgreSQL.\n' > "$$tmpdir/in.txt"; \
		./$(BIN_DIR)/$(TARGET) ai search postgresql copy csv | head -n 1; \
		./$(BIN_DIR)/$(TARGET) ai cite "file://$$tmpdir/page.html" --style apa | head -n 2; \
		if curl -sS --connect-timeout 1 --max-time 2 http://127.0.0.1:11434/api/tags >/dev/null 2>&1; then \
			echo "Ollama reachable; running ai summarize (ollama)..."; \
			DTCONVERT_AI_TIMEOUT=180 ./$(BIN_DIR)/$(TARGET) ai summarize "$$tmpdir/in.txt" --backend ollama --model "$${DTCONVERT_OLLAMA_MODEL:-llama3.1}" | head -n 20; \
		else \
			echo "Ollama not reachable; skipping ollama summarize."; \
		fi; \
		if [ -n "$${OPENAI_API_KEY:-}" ]; then \
			echo "OPENAI_API_KEY is set; running ai summarize (openai)..."; \
			set +e; \
			DTCONVERT_AI_TIMEOUT=60 ./$(BIN_DIR)/$(TARGET) ai summarize "$$tmpdir/in.txt" --backend openai --model "$${DTCONVERT_OPENAI_MODEL:-gpt-4o-mini}" >"$$tmpdir/openai.out" 2>"$$tmpdir/openai.err"; \
			rc=$$?; \
			set -e; \
			if [ $$rc -ne 0 ]; then \
				echo "OpenAI summarize failed (strict): $$(head -n 1 "$$tmpdir/openai.err")"; \
				exit $$rc; \
			fi; \
			head -n 20 "$$tmpdir/openai.out"; \
		else \
			echo "OPENAI_API_KEY not set; skipping openai summarize."; \
		fi; \
		echo "AI smoke (strict) completed"

# Debug build
debug: OPT_CFLAGS = -O0 -g3
debug: CFLAGS += -DDEBUG
debug: clean all

# Help
help:
	@echo "Available targets:"
	@echo "  all       - Build everything (default)"
	@echo "  clean     - Remove build files"
	@echo "  install   - Install (default PREFIX=/usr/local; override PREFIX/DESTDIR)"
	@echo "  uninstall - Uninstall (uses PREFIX/DESTDIR)"
	@echo "  test      - Run basic tests"
	@echo "  smoke     - Run repeatable smoke tests"
	@echo "  ai-smoke  - Run repeatable AI smoke tests"
	@echo "  ai-smoke-strict - AI smoke tests that fail on OpenAI errors"
	@echo "  samples   - Create sample converter scripts"
	@echo "  debug     - Build with debug symbols"
	@echo ""
	@echo "Install variables:"
	@echo "  PREFIX  - install prefix (e.g. $$HOME/.local)"
	@echo "  DESTDIR - staging root (e.g. package builds)"

.PHONY: all clean install uninstall test smoke ai-smoke ai-smoke-strict help directories modules samples debug