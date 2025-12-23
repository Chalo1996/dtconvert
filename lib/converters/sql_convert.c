#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void die(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) die("out of memory");
    return q;
}

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)xmalloc(n);
    memcpy(out, s, n);
    return out;
}

static void rstrip(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void lower_ascii(char *s) {
    for (; s && *s; s++) *s = (char)tolower((unsigned char)*s);
}

static bool is_ident(const char *s) {
    if (!s || !*s) return false;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (const char *p = s + 1; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_')) return false;
    }
    return true;
}

static char *escape_sql_literal(const char *s) {
    // duplicate single quotes
    size_t extra = 0;
    for (const char *p = s ? s : ""; *p; p++) {
        if (*p == '\'') extra++;
    }
    size_t n = strlen(s ? s : "");
    char *out = (char *)xmalloc(n + extra + 1);
    size_t j = 0;
    for (const char *p = s ? s : ""; *p; p++) {
        out[j++] = *p;
        if (*p == '\'') out[j++] = '\'';
    }
    out[j] = '\0';
    return out;
}

// ---------------- CSV reader/writer (minimal RFC4180-ish) ----------------

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StrList;

static void sl_push(StrList *sl, char *s) {
    if (sl->len + 1 > sl->cap) {
        sl->cap = sl->cap ? sl->cap * 2 : 16;
        sl->items = (char **)xrealloc(sl->items, sl->cap * sizeof(char *));
    }
    sl->items[sl->len++] = s;
}

static void sl_free(StrList *sl) {
    if (!sl) return;
    for (size_t i = 0; i < sl->len; i++) free(sl->items[i]);
    free(sl->items);
    memset(sl, 0, sizeof(*sl));
}

typedef struct {
    char ***rows;
    size_t nrows;
    size_t cap;

    char **header;
    size_t ncols;
} Csv;

static void csv_free(Csv *c) {
    if (!c) return;
    for (size_t i = 0; i < c->ncols; i++) free(c->header[i]);
    free(c->header);

    for (size_t r = 0; r < c->nrows; r++) {
        for (size_t j = 0; j < c->ncols; j++) free(c->rows[r][j]);
        free(c->rows[r]);
    }
    free(c->rows);
    memset(c, 0, sizeof(*c));
}

static char *csv_parse_field(const char **p) {
    const char *s = *p;
    bool quoted = false;

    if (*s == '"') {
        quoted = true;
        s++;
    }

    char *out = NULL;
    size_t cap = 0, len = 0;

    while (*s) {
        char ch = *s;
        if (quoted) {
            if (ch == '"') {
                s++;
                if (*s == '"') {
                    ch = '"';
                    s++;
                } else {
                    quoted = false;
                    break;
                }
            } else {
                s++;
            }
        } else {
            if (ch == ',' || ch == '\n' || ch == '\r') break;
            s++;
        }

        if (len + 2 > cap) {
            cap = cap ? cap * 2 : 64;
            out = (char *)xrealloc(out, cap);
        }
        out[len++] = ch;
    }

    if (!out) {
        out = (char *)xmalloc(1);
        out[0] = '\0';
    } else {
        out[len] = '\0';
    }

    *p = s;
    return out;
}

static void csv_consume_delim(const char **p) {
    const char *s = *p;
    if (*s == ',') {
        s++;
    } else if (*s == '\r') {
        s++;
        if (*s == '\n') s++;
    } else if (*s == '\n') {
        s++;
    }
    *p = s;
}

static bool csv_at_eol(const char *s) {
    return *s == '\0' || *s == '\n' || *s == '\r';
}

static int csv_read_all(const char *path, Csv *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    // Read full file into memory (small-tool assumption)
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 1;
    }
    rewind(f);

    char *buf = (char *)xmalloc((size_t)sz + 1);
    size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[nread] = '\0';

    const char *p = buf;
    // skip leading empty lines
    while (*p) {
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '\r' || *q == '\n') {
            csv_consume_delim(&q);
            p = q;
            continue;
        }
        break;
    }

    // header
    StrList hdr = {0};
    while (*p) {
        char *field = csv_parse_field(&p);
        sl_push(&hdr, field);
        if (csv_at_eol(p)) {
            csv_consume_delim(&p);
            break;
        }
        if (*p == ',') {
            csv_consume_delim(&p);
            continue;
        }
    }

    if (hdr.len == 0) {
        free(buf);
        return 1;
    }

    out->ncols = hdr.len;
    out->header = hdr.items;
    // transfer ownership
    hdr.items = NULL;
    hdr.len = hdr.cap = 0;

    // rows
    while (*p) {
        // skip empty lines
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == '\r' || *q == '\n') {
            csv_consume_delim(&q);
            p = q;
            continue;
        }

        char **row = (char **)xmalloc(out->ncols * sizeof(char *));
        for (size_t c = 0; c < out->ncols; c++) row[c] = xstrdup("");

        for (size_t c = 0; c < out->ncols; c++) {
            char *field = csv_parse_field(&p);
            free(row[c]);
            row[c] = field;

            if (csv_at_eol(p)) {
                csv_consume_delim(&p);
                break;
            }
            if (*p == ',') {
                csv_consume_delim(&p);
                continue;
            }
        }

        if (out->nrows + 1 > out->cap) {
            out->cap = out->cap ? out->cap * 2 : 16;
            out->rows = (char ***)xrealloc(out->rows, out->cap * sizeof(char **));
        }
        out->rows[out->nrows++] = row;

        // consume rest of line if any
        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p) csv_consume_delim(&p);
    }

    free(buf);
    return 0;
}

static void csv_write_escaped(FILE *f, const char *s) {
    bool need_quote = false;
    for (const char *p = s ? s : ""; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            need_quote = true;
            break;
        }
    }

    if (!need_quote) {
        fputs(s ? s : "", f);
        return;
    }

    fputc('"', f);
    for (const char *p = s ? s : ""; *p; p++) {
        if (*p == '"') fputc('"', f);
        fputc(*p, f);
    }
    fputc('"', f);
}

static int csv_write(const char *path, char **header, size_t ncols, char ***rows, size_t nrows) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    for (size_t c = 0; c < ncols; c++) {
        if (c) fputc(',', f);
        csv_write_escaped(f, header[c]);
    }
    fputc('\n', f);

    for (size_t r = 0; r < nrows; r++) {
        for (size_t c = 0; c < ncols; c++) {
            if (c) fputc(',', f);
            csv_write_escaped(f, rows[r][c]);
        }
        fputc('\n', f);
    }

    fclose(f);
    return 0;
}

// ---------------- SQL generation/parsing ----------------

static int csv_to_sql(const char *in_csv, const char *out_sql, const char *table, bool create_table) {
    if (!is_ident(table)) {
        fprintf(stderr, "Error: Invalid SQL identifier: %s\n", table);
        return 1;
    }

    Csv csv = {0};
    if (csv_read_all(in_csv, &csv) != 0) {
        csv_free(&csv);
        return 1;
    }

    if (csv.ncols == 0) {
        fprintf(stderr, "Error: CSV header row is empty\n");
        csv_free(&csv);
        return 1;
    }

    // Normalize column identifiers
    for (size_t i = 0; i < csv.ncols; i++) {
        char *col = csv.header[i];
        // Trim spaces
        while (*col && isspace((unsigned char)*col)) col++;
        if (*col == '\0') {
            free(csv.header[i]);
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "col%zu", i + 1);
            csv.header[i] = xstrdup(tmp);
        } else if (col != csv.header[i]) {
            char *newc = xstrdup(col);
            free(csv.header[i]);
            csv.header[i] = newc;
        }

        if (!is_ident(csv.header[i])) {
            fprintf(stderr, "Error: Invalid SQL identifier: %s\n", csv.header[i]);
            csv_free(&csv);
            return 1;
        }
    }

    FILE *out = fopen(out_sql, "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot write '%s': %s\n", out_sql, strerror(errno));
        csv_free(&csv);
        return 1;
    }

    fprintf(out, "-- Generated by dtconvert (csv -> sql)\n");

    if (create_table) {
        fprintf(out, "CREATE TABLE IF NOT EXISTS %s (", table);
        for (size_t i = 0; i < csv.ncols; i++) {
            if (i) fputs(", ", out);
            fprintf(out, "%s TEXT", csv.header[i]);
        }
        fputs(");\n", out);
    }

    // INSERT lines
    for (size_t r = 0; r < csv.nrows; r++) {
        fprintf(out, "INSERT INTO %s (", table);
        for (size_t c = 0; c < csv.ncols; c++) {
            if (c) fputs(", ", out);
            fputs(csv.header[c], out);
        }
        fputs(") VALUES (", out);

        for (size_t c = 0; c < csv.ncols; c++) {
            if (c) fputs(", ", out);
            char *esc = escape_sql_literal(csv.rows[r][c]);
            fputc('\'', out);
            fputs(esc, out);
            fputc('\'', out);
            free(esc);
        }
        fputs(");\n", out);
    }

    fputc('\n', out);
    fclose(out);
    csv_free(&csv);
    return 0;
}

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static bool match_ci(const char **p, const char *kw) {
    const char *s = *p;
    while (*kw) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*kw)) return false;
        s++;
        kw++;
    }
    *p = s;
    return true;
}

static char *parse_ident(const char **p) {
    skip_ws(p);
    const char *s = *p;
    if (!(isalpha((unsigned char)*s) || *s == '_')) return NULL;
    s++;
    while (*s && (isalnum((unsigned char)*s) || *s == '_')) s++;
    size_t n = (size_t)(s - *p);
    char *out = (char *)xmalloc(n + 1);
    memcpy(out, *p, n);
    out[n] = '\0';
    *p = s;
    return out;
}

static bool consume_char(const char **p, char ch) {
    skip_ws(p);
    if (**p == ch) {
        (*p)++;
        return true;
    }
    return false;
}

static char *parse_sql_string_literal(const char **p) {
    skip_ws(p);
    if (**p != '\'') return NULL;
    (*p)++;

    char *out = NULL;
    size_t cap = 0, len = 0;

    while (**p) {
        char ch = **p;
        if (ch == '\'') {
            (*p)++;
            if (**p == '\'') {
                // escaped quote
                ch = '\'';
                (*p)++;
            } else {
                break;
            }
        } else {
            (*p)++;
        }

        if (len + 2 > cap) {
            cap = cap ? cap * 2 : 64;
            out = (char *)xrealloc(out, cap);
        }
        out[len++] = ch;
    }

    if (!out) {
        out = (char *)xmalloc(1);
        out[0] = '\0';
    } else {
        out[len] = '\0';
    }
    return out;
}

static int sql_to_csv(const char *in_sql, const char *out_csv) {
    FILE *f = fopen(in_sql, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", in_sql, strerror(errno));
        return 1;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    StrList columns = {0};
    char ***rows = NULL;
    size_t nrows = 0, rcap = 0;

    while ((n = getline(&line, &cap, f)) >= 0) {
        (void)n;
        rstrip(line);
        const char *p = line;
        skip_ws(&p);
        if (*p == '\0' || *p == '-') continue;

        // INSERT INTO <table> (<cols>) VALUES (<vals>);
        if (!match_ci(&p, "insert")) continue;
        skip_ws(&p);
        if (!match_ci(&p, "into")) continue;

        char *table = parse_ident(&p);
        free(table);

        if (!consume_char(&p, '(')) continue;

        // parse columns
        StrList cols_this = {0};
        while (true) {
            char *col = parse_ident(&p);
            if (!col) {
                sl_free(&cols_this);
                goto next_line;
            }
            sl_push(&cols_this, col);
            if (consume_char(&p, ')')) break;
            if (!consume_char(&p, ',')) {
                sl_free(&cols_this);
                goto next_line;
            }
        }

        skip_ws(&p);
        if (!match_ci(&p, "values")) {
            sl_free(&cols_this);
            goto next_line;
        }
        if (!consume_char(&p, '(')) {
            sl_free(&cols_this);
            goto next_line;
        }

        // parse values (single-quoted literals only)
        StrList vals = {0};
        while (true) {
            char *v = parse_sql_string_literal(&p);
            if (!v) {
                sl_free(&vals);
                sl_free(&cols_this);
                goto next_line;
            }
            sl_push(&vals, v);
            if (consume_char(&p, ')')) break;
            if (!consume_char(&p, ',')) {
                sl_free(&vals);
                sl_free(&cols_this);
                goto next_line;
            }
        }

        (void)consume_char(&p, ';');

        if (columns.len == 0) {
            // adopt first column set
            columns = cols_this;
            cols_this.items = NULL;
            cols_this.len = cols_this.cap = 0;
        } else {
            // ensure same column set
            if (columns.len != cols_this.len) {
                sl_free(&vals);
                sl_free(&cols_this);
                free(line);
                fclose(f);
                fprintf(stderr, "Error: SQL contains mixed column sets; not supported in MVP\n");
                // free columns/rows
                for (size_t r = 0; r < nrows; r++) {
                    for (size_t c = 0; c < columns.len; c++) free(rows[r][c]);
                    free(rows[r]);
                }
                free(rows);
                sl_free(&columns);
                return 1;
            }
            for (size_t i = 0; i < columns.len; i++) {
                if (strcmp(columns.items[i], cols_this.items[i]) != 0) {
                    sl_free(&vals);
                    sl_free(&cols_this);
                    free(line);
                    fclose(f);
                    fprintf(stderr, "Error: SQL contains mixed column sets; not supported in MVP\n");
                    for (size_t r = 0; r < nrows; r++) {
                        for (size_t c = 0; c < columns.len; c++) free(rows[r][c]);
                        free(rows[r]);
                    }
                    free(rows);
                    sl_free(&columns);
                    return 1;
                }
            }
        }
        sl_free(&cols_this);

        // store row
        if (vals.len < columns.len) {
            // pad
            while (vals.len < columns.len) sl_push(&vals, xstrdup(""));
        }

        char **row = (char **)xmalloc(columns.len * sizeof(char *));
        for (size_t i = 0; i < columns.len; i++) {
            row[i] = vals.items[i];
            vals.items[i] = NULL;
        }
        // free leftover
        for (size_t i = columns.len; i < vals.len; i++) free(vals.items[i]);
        free(vals.items);

        if (nrows + 1 > rcap) {
            rcap = rcap ? rcap * 2 : 32;
            rows = (char ***)xrealloc(rows, rcap * sizeof(char **));
        }
        rows[nrows++] = row;

    next_line:
        continue;
    }

    free(line);
    fclose(f);

    if (columns.len == 0) {
        fprintf(stderr, "Error: No INSERT statements found (this MVP only parses INSERTs generated by dtconvert)\n");
        return 1;
    }

    int rc = csv_write(out_csv, columns.items, columns.len, rows, nrows);

    for (size_t r = 0; r < nrows; r++) {
        for (size_t c = 0; c < columns.len; c++) free(rows[r][c]);
        free(rows[r]);
    }
    free(rows);
    sl_free(&columns);

    return rc;
}

static void usage(void) {
    fprintf(stderr,
            "Usage: sql_convert <csv-to-sql|sql-to-csv> <input> <output> [--table NAME] [--create]\n");
}

int main(int argc, char **argv) {
    if (argc < 4) {
        usage();
        return 2;
    }

    const char *cmd = argv[1];
    const char *in_path = argv[2];
    const char *out_path = argv[3];

    char *table = xstrdup("data");
    bool create_table = false;

    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--table") == 0) {
            if (i + 1 >= argc) {
                free(table);
                fprintf(stderr, "Error: --table requires a value\n");
                return 2;
            }
            free(table);
            table = xstrdup(argv[i + 1]);
            i++;
            continue;
        }
        if (strcmp(argv[i], "--create") == 0) {
            create_table = true;
            continue;
        }
        free(table);
        fprintf(stderr, "Error: Unknown argument: %s\n", argv[i]);
        return 2;
    }

    lower_ascii((char *)cmd);

    int rc;
    if (strcmp(cmd, "csv-to-sql") == 0) {
        rc = csv_to_sql(in_path, out_path, table, create_table);
    } else if (strcmp(cmd, "sql-to-csv") == 0) {
        rc = sql_to_csv(in_path, out_path);
    } else {
        usage();
        rc = 2;
    }

    free(table);
    return rc == 0 ? 0 : 1;
}
