#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EXT_LEN 16

typedef struct {
    char **headers;
    size_t ncols;

    char ***rows;
    size_t nrows;
} Table;

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

static void table_free(Table *t) {
    if (!t) return;

    for (size_t i = 0; i < t->ncols; i++) free(t->headers[i]);
    free(t->headers);

    for (size_t r = 0; r < t->nrows; r++) {
        if (!t->rows[r]) continue;
        for (size_t c = 0; c < t->ncols; c++) free(t->rows[r][c]);
        free(t->rows[r]);
    }
    free(t->rows);

    memset(t, 0, sizeof(*t));
}

static void rstrip(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1]))) {
        s[n - 1] = '\0';
        n--;
    }
}

static char *lskip(char *s) {
    while (s && *s && isspace((unsigned char)*s)) s++;
    return s;
}

static int find_header(const Table *t, const char *key) {
    for (size_t i = 0; i < t->ncols; i++) {
        if (strcmp(t->headers[i], key) == 0) return (int)i;
    }
    return -1;
}

static void ensure_col(Table *t, const char *key) {
    if (find_header(t, key) >= 0) return;

    t->headers = (char **)xrealloc(t->headers, (t->ncols + 1) * sizeof(char *));
    t->headers[t->ncols] = xstrdup(key);

    // Expand existing rows
    for (size_t r = 0; r < t->nrows; r++) {
        t->rows[r] = (char **)xrealloc(t->rows[r], (t->ncols + 1) * sizeof(char *));
        t->rows[r][t->ncols] = xstrdup("");
    }

    t->ncols++;
}

static size_t table_add_row(Table *t) {
    t->rows = (char ***)xrealloc(t->rows, (t->nrows + 1) * sizeof(char **));
    t->rows[t->nrows] = (char **)xmalloc(t->ncols * sizeof(char *));
    for (size_t c = 0; c < t->ncols; c++) t->rows[t->nrows][c] = xstrdup("");
    t->nrows++;
    return t->nrows - 1;
}

static void table_set(Table *t, size_t row, const char *key, const char *value) {
    ensure_col(t, key);
    int idx = find_header(t, key);
    if (idx < 0) return;

    free(t->rows[row][(size_t)idx]);
    t->rows[row][(size_t)idx] = xstrdup(value ? value : "");
}

static const char *path_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path || dot[1] == '\0') return "";
    return dot + 1;
}

static void lower_ascii(char *s) {
    for (; s && *s; s++) *s = (char)tolower((unsigned char)*s);
}

// ---------------- CSV ----------------

typedef struct {
    const char *data;
    size_t len;
    size_t pos;
} Cursor;

static bool cur_eof(const Cursor *c) { return c->pos >= c->len; }

static char cur_peek(const Cursor *c) { return cur_eof(c) ? '\0' : c->data[c->pos]; }

static char cur_get(Cursor *c) { return cur_eof(c) ? '\0' : c->data[c->pos++]; }

static char *read_file_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)xmalloc((size_t)sz + 1);
    size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[nread] = '\0';
    if (out_len) *out_len = nread;
    return buf;
}

static char *csv_parse_field(Cursor *c) {
    // RFC4180-ish: quoted fields, double quotes escaping
    // Does NOT consume line terminators. Consumes a single comma delimiter for quoted fields.
    char *out = NULL;
    size_t cap = 0, n = 0;

    bool quoted = false;
    if (cur_peek(c) == '"') {
        quoted = true;
        (void)cur_get(c);
    }

    while (!cur_eof(c)) {
        char ch = cur_peek(c);
        if (quoted) {
            if (ch == '"') {
                (void)cur_get(c);
                if (cur_peek(c) == '"') {
                    (void)cur_get(c);
                    ch = '"';
                } else {
                    // end quote
                    quoted = false;
                    // next must be comma or newline or EOF; consume optional spaces
                    while (!cur_eof(c) && (cur_peek(c) == ' ' || cur_peek(c) == '\t')) (void)cur_get(c);
                    break;
                }
            }
        } else {
            if (ch == ',' || ch == '\n' || ch == '\r') break;
        }

        // consume and append
        ch = cur_get(c);

        if (n + 2 > cap) {
            cap = cap ? cap * 2 : 64;
            out = (char *)xrealloc(out, cap);
        }
        out[n++] = ch;
    }

    if (!out) {
        out = (char *)xmalloc(1);
        out[0] = '\0';
        return out;
    }
    out[n] = '\0';

    // If we just ended a quoted field, consume a single comma delimiter (leave newlines for caller).
    if (!quoted) {
        // quoted==false also true for unquoted; only consume comma if present.
        if (cur_peek(c) == ',') (void)cur_get(c);
    }

    return out;
}

static bool csv_consume_newline(Cursor *c) {
    if (cur_peek(c) == '\n') {
        (void)cur_get(c);
        return true;
    }
    if (cur_peek(c) == '\r') {
        (void)cur_get(c);
        if (cur_peek(c) == '\n') (void)cur_get(c);
        return true;
    }
    return false;
}

static bool csv_skip_empty_lines(Cursor *c) {
    size_t start = c->pos;
    while (true) {
        size_t p = c->pos;
        while (!cur_eof(c) && (cur_peek(c) == ' ' || cur_peek(c) == '\t')) (void)cur_get(c);
        if (csv_consume_newline(c)) {
            start = c->pos;
            continue;
        }
        c->pos = p;
        break;
    }
    return c->pos != start;
}

static int csv_read_table(const char *path, Table *out) {
    memset(out, 0, sizeof(*out));
    size_t len = 0;
    char *buf = read_file_all(path, &len);
    if (!buf) return 1;

    Cursor c = {.data = buf, .len = len, .pos = 0};
    csv_skip_empty_lines(&c);

    // header
    while (!cur_eof(&c)) {
        char *field = csv_parse_field(&c);
        rstrip(field);
        char *trimmed = lskip(field);
        // Normalize by copying trimmed into field
        if (trimmed != field) memmove(field, trimmed, strlen(trimmed) + 1);
        ensure_col(out, field);
        free(field);

        if (cur_peek(&c) == ',') {
            (void)cur_get(&c);
            continue;
        }
        if (csv_consume_newline(&c)) break;
        if (cur_eof(&c)) break;
    }

    if (out->ncols == 0) {
        free(buf);
        return 1;
    }

    // rows
    while (!cur_eof(&c)) {
        csv_skip_empty_lines(&c);
        if (cur_eof(&c)) break;

        size_t row = table_add_row(out);
        bool line_done = false;
        for (size_t col = 0; col < out->ncols; col++) {
            char *field = csv_parse_field(&c);
            rstrip(field);
            table_set(out, row, out->headers[col], field);
            free(field);

            if (cur_peek(&c) == ',') {
                (void)cur_get(&c);
                continue;
            }
            if (cur_peek(&c) == '\n' || cur_peek(&c) == '\r') {
                (void)csv_consume_newline(&c);
                line_done = true;
                break;
            }
            if (cur_eof(&c)) break;
        }
        // consume to end of line if there are extra fields
        if (!line_done) {
            while (!cur_eof(&c) && cur_peek(&c) != '\n' && cur_peek(&c) != '\r') (void)cur_get(&c);
            (void)csv_consume_newline(&c);
        }
    }

    free(buf);
    return 0;
}

static void csv_write_escaped(FILE *f, const char *s) {
    bool need_quote = false;
    for (const char *p = s; p && *p; p++) {
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
    for (const char *p = s; p && *p; p++) {
        if (*p == '"') fputc('"', f);
        fputc(*p, f);
    }
    fputc('"', f);
}

static int csv_write_table(const char *path, const Table *t) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    for (size_t c = 0; c < t->ncols; c++) {
        if (c) fputc(',', f);
        csv_write_escaped(f, t->headers[c]);
    }
    fputc('\n', f);

    for (size_t r = 0; r < t->nrows; r++) {
        for (size_t c = 0; c < t->ncols; c++) {
            if (c) fputc(',', f);
            csv_write_escaped(f, t->rows[r][c] ? t->rows[r][c] : "");
        }
        fputc('\n', f);
    }

    fclose(f);
    return 0;
}

// ---------------- JSON (minimal) ----------------

typedef struct {
    const char *s;
    size_t n;
    size_t i;
} J;

static void jskip(J *j) {
    while (j->i < j->n && isspace((unsigned char)j->s[j->i])) j->i++;
}

static bool jmatch(J *j, char ch) {
    jskip(j);
    if (j->i < j->n && j->s[j->i] == ch) {
        j->i++;
        return true;
    }
    return false;
}

static void jexpect(J *j, char ch) {
    if (!jmatch(j, ch)) {
        fprintf(stderr, "Error: JSON parse error: expected '%c'\n", ch);
        exit(1);
    }
}

static char *jparse_string(J *j) {
    jskip(j);
    if (j->i >= j->n || j->s[j->i] != '"') die("JSON parse error: expected string");
    j->i++;

    char *out = NULL;
    size_t cap = 0, len = 0;

    while (j->i < j->n) {
        char ch = j->s[j->i++];
        if (ch == '"') break;
        if (ch == '\\') {
            if (j->i >= j->n) die("JSON parse error: bad escape");
            char esc = j->s[j->i++];
            switch (esc) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u':
                    // Minimal: skip \uXXXX and emit '?' (keeps output valid)
                    if (j->i + 4 <= j->n) j->i += 4;
                    ch = '?';
                    break;
                default:
                    die("JSON parse error: unsupported escape");
            }
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
        return out;
    }
    out[len] = '\0';
    return out;
}

static char *jparse_primitive_as_string(J *j) {
    jskip(j);
    size_t start = j->i;

    if (j->i < j->n && j->s[j->i] == '"') return jparse_string(j);

    while (j->i < j->n) {
        char ch = j->s[j->i];
        if (ch == ',' || ch == '}' || ch == ']' || isspace((unsigned char)ch)) break;
        j->i++;
    }

    if (j->i == start) return xstrdup("");

    size_t len = j->i - start;
    char *out = (char *)xmalloc(len + 1);
    memcpy(out, j->s + start, len);
    out[len] = '\0';
    return out;
}

static int json_read_table(const char *path, Table *out) {
    memset(out, 0, sizeof(*out));
    size_t len = 0;
    char *buf = read_file_all(path, &len);
    if (!buf) return 1;

    J j = {.s = buf, .n = len, .i = 0};
    jexpect(&j, '[');
    jskip(&j);

    if (jmatch(&j, ']')) {
        // empty array
        free(buf);
        return 0;
    }

    while (true) {
        jexpect(&j, '{');
        // ensure at least one row exists
        if (out->ncols == 0) {
            // allow headers to be created by first object
        }
        size_t row = table_add_row(out);

        jskip(&j);
        if (jmatch(&j, '}')) {
            // empty object
        } else {
            while (true) {
                char *key = jparse_string(&j);
                jexpect(&j, ':');
                char *val = jparse_primitive_as_string(&j);
                table_set(out, row, key, val);
                free(key);
                free(val);

                jskip(&j);
                if (jmatch(&j, '}')) break;
                jexpect(&j, ',');
            }
        }

        jskip(&j);
        if (jmatch(&j, ']')) break;
        jexpect(&j, ',');
    }

    free(buf);
    return 0;
}

static void json_write_escaped(FILE *f, const char *s) {
    fputc('"', f);
    for (const char *p = s ? s : ""; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        switch (ch) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f); break;
            case '\f': fputs("\\f", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (ch < 0x20) {
                    fprintf(f, "\\u%04x", (unsigned int)ch);
                } else {
                    fputc((char)ch, f);
                }
        }
    }
    fputc('"', f);
}

static int json_write_table(const char *path, const Table *t) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    fputs("[\n", f);
    for (size_t r = 0; r < t->nrows; r++) {
        fputs("  {", f);
        for (size_t c = 0; c < t->ncols; c++) {
            if (c) fputs(", ", f);
            json_write_escaped(f, t->headers[c]);
            fputs(": ", f);
            json_write_escaped(f, t->rows[r][c] ? t->rows[r][c] : "");
        }
        fputs("}", f);
        if (r + 1 < t->nrows) fputs(",", f);
        fputs("\n", f);
    }
    fputs("]\n", f);

    fclose(f);
    return 0;
}

// ---------------- YAML (very small subset) ----------------
// Supported YAML shape:
// - key: "value"
//   key2: "value"
// Values may be quoted (recommended) or unquoted single-line scalars.

static char *yaml_parse_value(char *s) {
    s = lskip(s);
    rstrip(s);
    if (*s == '"') {
        s++;
        char *out = NULL;
        size_t cap = 0, len = 0;
        while (*s && *s != '"') {
            char ch = *s++;
            if (ch == '\\') {
                char esc = *s++;
                switch (esc) {
                    case '"': ch = '"'; break;
                    case '\\': ch = '\\'; break;
                    case 'n': ch = '\n'; break;
                    case 'r': ch = '\r'; break;
                    case 't': ch = '\t'; break;
                    default: ch = esc; break;
                }
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
            return out;
        }
        out[len] = '\0';
        return out;
    }
    return xstrdup(s);
}

static int yaml_read_table(const char *path, Table *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    size_t current_row = (size_t)-1;

    while ((n = getline(&line, &cap, f)) >= 0) {
        (void)n;
        rstrip(line);
        char *p = lskip(line);
        if (*p == '\0') continue;
        if (*p == '#') continue;

        if (strncmp(p, "- ", 2) == 0) {
            // new record
            p += 2;
            if (out->ncols == 0) {
                // allow first keys to define columns
            }
            current_row = table_add_row(out);
            p = lskip(p);
            if (*p == '\0') continue;
            // optional inline key: value
        }

        if (current_row == (size_t)-1) {
            free(line);
            fclose(f);
            die("YAML parse error: expected '- ' to start a record");
        }

        // Allow indentation
        p = lskip(p);
        char *colon = strchr(p, ':');
        if (!colon) continue;

        *colon = '\0';
        char *key = p;
        rstrip(key);
        char *val_part = colon + 1;

        char *val = yaml_parse_value(val_part);
        table_set(out, current_row, key, val);
        free(val);
    }

    free(line);
    fclose(f);
    return 0;
}

static void yaml_write_escaped(FILE *f, const char *s) {
    fputc('"', f);
    for (const char *p = s ? s : ""; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        switch (ch) {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default: fputc((char)ch, f); break;
        }
    }
    fputc('"', f);
}

static int yaml_write_table(const char *path, const Table *t) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    for (size_t r = 0; r < t->nrows; r++) {
        fputs("- ", f);
        if (t->ncols > 0) {
            fputs(t->headers[0], f);
            fputs(": ", f);
            yaml_write_escaped(f, t->rows[r][0] ? t->rows[r][0] : "");
            fputc('\n', f);
        } else {
            fputs("{}\n", f);
        }
        for (size_t c = 1; c < t->ncols; c++) {
            fputs("  ", f);
            fputs(t->headers[c], f);
            fputs(": ", f);
            yaml_write_escaped(f, t->rows[r][c] ? t->rows[r][c] : "");
            fputc('\n', f);
        }
    }

    fclose(f);
    return 0;
}

// ---------------- Dispatch ----------------

static int read_table(const char *path, const char *ext, Table *t) {
    if (strcmp(ext, "csv") == 0) return csv_read_table(path, t);
    if (strcmp(ext, "json") == 0) return json_read_table(path, t);
    if (strcmp(ext, "yaml") == 0 || strcmp(ext, "yml") == 0) return yaml_read_table(path, t);
    fprintf(stderr, "Error: unsupported input format: %s\n", ext);
    return 1;
}

static int write_table(const char *path, const char *ext, const Table *t) {
    if (strcmp(ext, "csv") == 0) return csv_write_table(path, t);
    if (strcmp(ext, "json") == 0) return json_write_table(path, t);
    if (strcmp(ext, "yaml") == 0 || strcmp(ext, "yml") == 0) return yaml_write_table(path, t);
    fprintf(stderr, "Error: unsupported output format: %s\n", ext);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: data_convert <input.(csv|json|yaml)> <output.(csv|json|yaml)>\n");
        return 2;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];

    char in_ext[MAX_EXT_LEN] = {0};
    char out_ext[MAX_EXT_LEN] = {0};

    snprintf(in_ext, sizeof(in_ext), "%s", path_ext(in_path));
    snprintf(out_ext, sizeof(out_ext), "%s", path_ext(out_path));
    lower_ascii(in_ext);
    lower_ascii(out_ext);

    if (strcmp(in_ext, "yml") == 0) snprintf(in_ext, sizeof(in_ext), "%s", "yaml");
    if (strcmp(out_ext, "yml") == 0) snprintf(out_ext, sizeof(out_ext), "%s", "yaml");

    Table t = {0};
    if (read_table(in_path, in_ext, &t) != 0) {
        table_free(&t);
        return 1;
    }

    if (write_table(out_path, out_ext, &t) != 0) {
        table_free(&t);
        return 1;
    }

    table_free(&t);
    return 0;
}
