#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool ends_with_ci(const char *s, const char *suffix) {
    size_t ns = strlen(s);
    size_t nf = strlen(suffix);
    if (nf > ns) return false;
    const char *p = s + (ns - nf);
    for (size_t i = 0; i < nf; i++) {
        char a = (char)tolower((unsigned char)p[i]);
        char b = (char)tolower((unsigned char)suffix[i]);
        if (a != b) return false;
    }
    return true;
}

static void json_write_escaped(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        unsigned char ch = *p;
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

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} Tokens;

static void tokens_push(Tokens *t, const char *s, size_t n) {
    if (t->len + 1 > t->cap) {
        t->cap = t->cap ? t->cap * 2 : 128;
        t->items = (char **)xrealloc(t->items, t->cap * sizeof(char *));
    }
    char *tok = (char *)xmalloc(n + 1);
    memcpy(tok, s, n);
    tok[n] = '\0';
    t->items[t->len++] = tok;
}

static void tokens_free(Tokens *t) {
    if (!t) return;
    for (size_t i = 0; i < t->len; i++) free(t->items[i]);
    free(t->items);
    memset(t, 0, sizeof(*t));
}

static int read_all(const char *path, char **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }
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
    *out_buf = buf;
    *out_len = nread;
    return 0;
}

// Tokenization model (byte-based, ASCII classes):
// - sequences of [A-Za-z0-9_] are one token
// - any non-whitespace, non-word byte is its own token
static int tokenize_text(const char *buf, size_t len, Tokens *out) {
    memset(out, 0, sizeof(*out));

    size_t i = 0;
    while (i < len) {
        unsigned char ch = (unsigned char)buf[i];

        if (isspace(ch)) {
            i++;
            continue;
        }

        if (isalnum(ch) || ch == '_') {
            size_t start = i;
            i++;
            while (i < len) {
                unsigned char c2 = (unsigned char)buf[i];
                if (isalnum(c2) || c2 == '_') {
                    i++;
                } else {
                    break;
                }
            }
            tokens_push(out, buf + start, i - start);
            continue;
        }

        // punctuation / symbol
        tokens_push(out, buf + i, 1);
        i++;
    }

    return 0;
}

static int write_tokens_txt(const char *path, const Tokens *t) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    for (size_t i = 0; i < t->len; i++) {
        fputs(t->items[i], f);
        fputc('\n', f);
    }

    fclose(f);
    return 0;
}

static int write_tokens_json(const char *path, const Tokens *t) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    fputs("[\n", f);
    for (size_t i = 0; i < t->len; i++) {
        fputs("  ", f);
        json_write_escaped(f, t->items[i]);
        if (i + 1 < t->len) fputs(",", f);
        fputc('\n', f);
    }
    fputs("]\n", f);

    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: tokenize <input.txt> <output.(txt|json)>\n");
        return 2;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];

    char *buf = NULL;
    size_t len = 0;
    if (read_all(in_path, &buf, &len) != 0) return 1;

    Tokens t = {0};
    int rc = tokenize_text(buf, len, &t);
    free(buf);
    if (rc != 0) {
        tokens_free(&t);
        return 1;
    }

    int wrc;
    if (ends_with_ci(out_path, ".json")) {
        wrc = write_tokens_json(out_path, &t);
    } else {
        wrc = write_tokens_txt(out_path, &t);
    }

    tokens_free(&t);
    return wrc == 0 ? 0 : 1;
}
