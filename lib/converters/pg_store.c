#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_IDENT 128

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

static bool is_ident(const char *s) {
    if (!s || !*s) return false;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (const char *p = s + 1; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_')) return false;
    }
    return true;
}

static void sanitize_identifier(const char *raw, const char *fallback, char out[MAX_IDENT]) {
    const char *s = raw ? raw : "";
    while (*s && isspace((unsigned char)*s)) s++;

    if (*s == '\0') {
        snprintf(out, MAX_IDENT, "%s", fallback);
        return;
    }

    char tmp[MAX_IDENT];
    size_t j = 0;
    for (; *s && j + 1 < sizeof(tmp); s++) {
        unsigned char ch = (unsigned char)*s;
        if (isalnum(ch) || ch == '_') {
            tmp[j++] = (char)ch;
        } else {
            tmp[j++] = '_';
        }
    }
    tmp[j] = '\0';

    // collapse underscores
    char tmp2[MAX_IDENT];
    size_t k = 0;
    for (size_t i = 0; tmp[i] && k + 1 < sizeof(tmp2); i++) {
        if (tmp[i] == '_' && k > 0 && tmp2[k - 1] == '_') continue;
        tmp2[k++] = tmp[i];
    }
    tmp2[k] = '\0';

    // must start with alpha/_
    if (!(isalpha((unsigned char)tmp2[0]) || tmp2[0] == '_')) {
        // Keep output bounded and deterministic; avoid relying on implicit snprintf truncation.
        enum { PREFIX_MAX = (MAX_IDENT - 2) / 2, SUFFIX_MAX = (MAX_IDENT - 2) - PREFIX_MAX };
        snprintf(out, MAX_IDENT, "%.*s_%.*s", (int)PREFIX_MAX, fallback, (int)SUFFIX_MAX, tmp2);
    } else {
        snprintf(out, MAX_IDENT, "%.*s", (int)(MAX_IDENT - 1), tmp2);
    }

    if (!is_ident(out)) {
        snprintf(out, MAX_IDENT, "%s", fallback);
    }
}

static char *shutil_which(const char *cmd) {
    const char *path = getenv("PATH");
    if (!path) return NULL;

    const char *p = path;
    while (*p) {
        const char *sep = strchr(p, ':');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        size_t n = len + 1 + strlen(cmd) + 1;
        char *candidate = (char *)xmalloc(n);
        memcpy(candidate, p, len);
        candidate[len] = '\0';
        strcat(candidate, "/");
        strcat(candidate, cmd);

        if (access(candidate, X_OK) == 0) return candidate;
        free(candidate);

        if (!sep) break;
        p = sep + 1;
    }

    return NULL;
}

// ---------------- Minimal JSON parser for config ----------------

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

static bool jparse_bool(J *j, bool *out) {
    jskip(j);
    if (j->i + 4 <= j->n && strncmp(j->s + j->i, "true", 4) == 0) {
        j->i += 4;
        *out = true;
        return true;
    }
    if (j->i + 5 <= j->n && strncmp(j->s + j->i, "false", 5) == 0) {
        j->i += 5;
        *out = false;
        return true;
    }
    return false;
}

static void jskip_value(J *j) {
    // skip simple string/bool/null/number/object/array (best-effort)
    jskip(j);
    if (j->i >= j->n) return;
    char ch = j->s[j->i];
    if (ch == '"') {
        char *tmp = jparse_string(j);
        free(tmp);
        return;
    }
    if (ch == '{') {
        int depth = 0;
        while (j->i < j->n) {
            char c = j->s[j->i++];
            if (c == '"') {
                // skip string
                j->i--;
                char *tmp = jparse_string(j);
                free(tmp);
                continue;
            }
            if (c == '{') depth++;
            if (c == '}') {
                depth--;
                if (depth <= 0) break;
            }
        }
        return;
    }
    if (ch == '[') {
        int depth = 0;
        while (j->i < j->n) {
            char c = j->s[j->i++];
            if (c == '"') {
                j->i--;
                char *tmp = jparse_string(j);
                free(tmp);
                continue;
            }
            if (c == '[') depth++;
            if (c == ']') {
                depth--;
                if (depth <= 0) break;
            }
        }
        return;
    }

    // primitive
    while (j->i < j->n) {
        char c = j->s[j->i];
        if (c == ',' || c == '}' || c == ']') break;
        if (isspace((unsigned char)c)) break;
        j->i++;
    }
}

static char *read_all(const char *path, size_t *out_len) {
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

typedef struct {
    char *connection;
    char schema[MAX_IDENT];
    char table[MAX_IDENT];
    bool create_table;
    bool truncate;
    char *query;
} PgCfg;

static void cfg_init(PgCfg *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->schema, sizeof(cfg->schema), "%s", "public");
    snprintf(cfg->table, sizeof(cfg->table), "%s", "data");
    cfg->create_table = false;
    cfg->truncate = false;
}

static void cfg_free(PgCfg *cfg) {
    if (!cfg) return;
    free(cfg->connection);
    free(cfg->query);
    cfg_init(cfg);
}

static void parse_schema_table(const char *table, char schema_out[MAX_IDENT], char table_out[MAX_IDENT]) {
    const char *dot = strchr(table, '.');
    if (!dot) {
        sanitize_identifier(NULL, "public", schema_out);
        sanitize_identifier(table, "data", table_out);
        return;
    }

    // accept schema.table (first dot only)
    char s1[MAX_IDENT];
    char s2[MAX_IDENT];
    size_t n1 = (size_t)(dot - table);
    if (n1 >= sizeof(s1)) n1 = sizeof(s1) - 1;
    memcpy(s1, table, n1);
    s1[n1] = '\0';

    snprintf(s2, sizeof(s2), "%s", dot + 1);

    sanitize_identifier(s1, "public", schema_out);
    sanitize_identifier(s2, "data", table_out);
}

static int load_config(const char *path, PgCfg *cfg) {
    cfg_init(cfg);
    size_t len = 0;
    char *buf = read_all(path, &len);
    if (!buf) return 1;

    J j = {.s = buf, .n = len, .i = 0};
    jexpect(&j, '{');

    while (true) {
        jskip(&j);
        if (jmatch(&j, '}')) break;

        char *key = jparse_string(&j);
        jexpect(&j, ':');

        if (strcmp(key, "connection") == 0) {
            char *v = jparse_string(&j);
            free(cfg->connection);
            cfg->connection = v;
        } else if (strcmp(key, "schema") == 0) {
            char *v = jparse_string(&j);
            sanitize_identifier(v, "public", cfg->schema);
            free(v);
        } else if (strcmp(key, "table") == 0) {
            char *v = jparse_string(&j);
            char s[MAX_IDENT];
            char t[MAX_IDENT];
            parse_schema_table(v, s, t);
            snprintf(cfg->schema, sizeof(cfg->schema), "%s", s);
            snprintf(cfg->table, sizeof(cfg->table), "%s", t);
            free(v);
        } else if (strcmp(key, "create_table") == 0) {
            bool b;
            if (!jparse_bool(&j, &b)) {
                jskip_value(&j);
            } else {
                cfg->create_table = b;
            }
        } else if (strcmp(key, "truncate") == 0) {
            bool b;
            if (!jparse_bool(&j, &b)) {
                jskip_value(&j);
            } else {
                cfg->truncate = b;
            }
        } else if (strcmp(key, "query") == 0) {
            char *v = jparse_string(&j);
            // strip trailing semicolons/spaces
            while (*v) {
                size_t n = strlen(v);
                if (n == 0) break;
                char c = v[n - 1];
                if (c == ';' || isspace((unsigned char)c)) {
                    v[n - 1] = '\0';
                    continue;
                }
                break;
            }
            free(cfg->query);
            cfg->query = v;
        } else {
            jskip_value(&j);
        }

        free(key);

        jskip(&j);
        if (jmatch(&j, '}')) break;
        jexpect(&j, ',');
    }

    free(buf);

    if (!cfg->connection || cfg->connection[0] == '\0') {
        fprintf(stderr, "Error: Config requires a non-empty 'connection' string\n");
        cfg_free(cfg);
        return 1;
    }

    if ((!cfg->query || cfg->query[0] == '\0') && (!cfg->table[0])) {
        fprintf(stderr, "Error: Config requires either 'table' or 'query'\n");
        cfg_free(cfg);
        return 1;
    }

    return 0;
}

// ---------------- CSV header parsing for import ----------------

typedef struct {
    const char *s;
    size_t n;
    size_t i;
} Cur;

static bool ceof(const Cur *c) { return c->i >= c->n; }
static char cpeek(const Cur *c) { return ceof(c) ? '\0' : c->s[c->i]; }
static char cget(Cur *c) { return ceof(c) ? '\0' : c->s[c->i++]; }

static bool consume_newline(Cur *c) {
    if (cpeek(c) == '\n') {
        (void)cget(c);
        return true;
    }
    if (cpeek(c) == '\r') {
        (void)cget(c);
        if (cpeek(c) == '\n') (void)cget(c);
        return true;
    }
    return false;
}

static char *csv_field(Cur *c) {
    bool quoted = false;
    if (cpeek(c) == '"') {
        quoted = true;
        (void)cget(c);
    }

    char *out = NULL;
    size_t cap = 0, len = 0;

    while (!ceof(c)) {
        char ch = cpeek(c);
        if (quoted) {
            if (ch == '"') {
                (void)cget(c);
                if (cpeek(c) == '"') {
                    (void)cget(c);
                    ch = '"';
                } else {
                    quoted = false;
                    break;
                }
            }
        } else {
            if (ch == ',' || ch == '\n' || ch == '\r') break;
        }

        ch = cget(c);
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

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StrVec;

static void sv_push(StrVec *v, char *s) {
    if (v->len + 1 > v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->items = (char **)xrealloc(v->items, v->cap * sizeof(char *));
    }
    v->items[v->len++] = s;
}

static void sv_free(StrVec *v) {
    if (!v) return;
    for (size_t i = 0; i < v->len; i++) free(v->items[i]);
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static void make_unique_idents(StrVec *cols) {
    for (size_t i = 0; i < cols->len; i++) {
        // sanitize
        char tmp[MAX_IDENT];
        sanitize_identifier(cols->items[i], "col", tmp);
        free(cols->items[i]);
        cols->items[i] = xstrdup(tmp);

        // uniquify
        for (size_t j = 0; j < i; j++) {
            if (strcmp(cols->items[i], cols->items[j]) == 0) {
                // append _k
                int k = 2;
                char base[MAX_IDENT];
                snprintf(base, sizeof(base), "%s", cols->items[i]);
                while (true) {
                    char candidate[MAX_IDENT];
                    // Reserve room for "_" + up to 10 digits + NUL; keep truncation deterministic.
                    enum { BASE_MAX = MAX_IDENT - 12 };
                    snprintf(candidate, sizeof(candidate), "%.*s_%d", (int)BASE_MAX, base, k);
                    bool found = false;
                    for (size_t z = 0; z < i; z++) {
                        if (strcmp(candidate, cols->items[z]) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        free(cols->items[i]);
                        cols->items[i] = xstrdup(candidate);
                        break;
                    }
                    k++;
                }
            }
        }

        if (!is_ident(cols->items[i])) {
            free(cols->items[i]);
            cols->items[i] = xstrdup("col");
        }
    }
}

static int read_csv_header(const char *csv_path, StrVec *cols_out) {
    memset(cols_out, 0, sizeof(*cols_out));
    size_t len = 0;
    char *buf = read_all(csv_path, &len);
    if (!buf) return 1;

    Cur c = {.s = buf, .n = len, .i = 0};

    // skip empty lines
    while (!ceof(&c)) {
        size_t start = c.i;
        while (!ceof(&c) && (cpeek(&c) == ' ' || cpeek(&c) == '\t')) (void)cget(&c);
        if (consume_newline(&c)) continue;
        c.i = start;
        break;
    }

    while (!ceof(&c)) {
        char *field = csv_field(&c);
        sv_push(cols_out, field);
        if (cpeek(&c) == ',') {
            (void)cget(&c);
            continue;
        }
        (void)consume_newline(&c);
        break;
    }

    free(buf);

    if (cols_out->len == 0) {
        fprintf(stderr, "Error: CSV appears to be empty\n");
        sv_free(cols_out);
        return 1;
    }

    make_unique_idents(cols_out);
    return 0;
}

// ---------------- psql execution helpers ----------------

static int run_psql(const char *connection, const char *sql, const char *stdin_path, const char *stdout_path) {
    char *psql_path = shutil_which("psql");
    if (!psql_path) {
        fprintf(stderr, "Error: psql is required (install PostgreSQL client tools)\n");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        free(psql_path);
        return 1;
    }

    if (pid == 0) {
        if (stdin_path) {
            FILE *in = fopen(stdin_path, "rb");
            if (!in) {
                fprintf(stderr, "Error: cannot open '%s': %s\n", stdin_path, strerror(errno));
                _exit(1);
            }
            dup2(fileno(in), STDIN_FILENO);
            fclose(in);
        }

        if (stdout_path) {
            FILE *out = fopen(stdout_path, "wb");
            if (!out) {
                fprintf(stderr, "Error: cannot write '%s': %s\n", stdout_path, strerror(errno));
                _exit(1);
            }
            dup2(fileno(out), STDOUT_FILENO);
            fclose(out);
        }

        char *const argv[] = {
            psql_path,
            (char *)"-X", // do not read ~/.psqlrc (keeps behavior deterministic)
            (char *)"-w", // never prompt for password; rely on .pgpass / PGPASSWORD
            (char *)connection,
            (char *)"-v",
            (char *)"ON_ERROR_STOP=1",
            (char *)"-q",
            (char *)"-c",
            (char *)sql,
            NULL,
        };

        execv(psql_path, argv);
        fprintf(stderr, "Error: failed to execute psql: %s\n", strerror(errno));
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    free(psql_path);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

static int csv_to_postgresql(const char *csv_path, const char *config_path) {
    PgCfg cfg;
    if (load_config(config_path, &cfg) != 0) return 1;

    StrVec cols;
    if (read_csv_header(csv_path, &cols) != 0) {
        cfg_free(&cfg);
        return 1;
    }

    char fq[2 * MAX_IDENT + 4];
    snprintf(fq, sizeof(fq), "%s.%s", cfg.schema, cfg.table);

    if (cfg.create_table) {
        // Build CREATE TABLE
        size_t sqlcap = 1024 + cols.len * 64;
        char *sql = (char *)xmalloc(sqlcap);
        snprintf(sql, sqlcap, "CREATE TABLE IF NOT EXISTS %s (", fq);
        for (size_t i = 0; i < cols.len; i++) {
            if (i) strncat(sql, ", ", sqlcap - strlen(sql) - 1);
            strncat(sql, cols.items[i], sqlcap - strlen(sql) - 1);
            strncat(sql, " TEXT", sqlcap - strlen(sql) - 1);
        }
        strncat(sql, ");", sqlcap - strlen(sql) - 1);

        int rc = run_psql(cfg.connection, sql, NULL, NULL);
        free(sql);
        if (rc != 0) {
            sv_free(&cols);
            cfg_free(&cfg);
            return rc;
        }
    }

    if (cfg.truncate) {
        char sql[512];
        snprintf(sql, sizeof(sql), "TRUNCATE %s;", fq);
        int rc = run_psql(cfg.connection, sql, NULL, NULL);
        if (rc != 0) {
            sv_free(&cols);
            cfg_free(&cfg);
            return rc;
        }
    }

    // \copy schema.table (cols...) FROM STDIN WITH (FORMAT csv, HEADER true)
    size_t sqlcap = 512 + cols.len * 64;
    char *copy = (char *)xmalloc(sqlcap);
    snprintf(copy, sqlcap, "\\copy %s (", fq);
    for (size_t i = 0; i < cols.len; i++) {
        if (i) strncat(copy, ", ", sqlcap - strlen(copy) - 1);
        strncat(copy, cols.items[i], sqlcap - strlen(copy) - 1);
    }
    strncat(copy, ") FROM STDIN WITH (FORMAT csv, HEADER true)", sqlcap - strlen(copy) - 1);

    int rc = run_psql(cfg.connection, copy, csv_path, NULL);

    free(copy);
    sv_free(&cols);
    cfg_free(&cfg);
    return rc;
}

static int postgresql_to_csv(const char *config_path, const char *out_csv) {
    PgCfg cfg;
    if (load_config(config_path, &cfg) != 0) return 1;

    char sql[2048];
    if (cfg.query && cfg.query[0] != '\0') {
        snprintf(sql, sizeof(sql), "\\copy (%s) TO STDOUT WITH (FORMAT csv, HEADER true)", cfg.query);
    } else {
        char fq[2 * MAX_IDENT + 4];
        snprintf(fq, sizeof(fq), "%s.%s", cfg.schema, cfg.table);
        snprintf(sql, sizeof(sql), "\\copy %s TO STDOUT WITH (FORMAT csv, HEADER true)", fq);
    }

    int rc = run_psql(cfg.connection, sql, NULL, out_csv);
    cfg_free(&cfg);
    return rc;
}

static void usage(void) {
    fprintf(stderr,
            "Usage: pg_store <csv-to-postgresql|postgresql-to-csv> <input> <output>\n"
            "  csv-to-postgresql: <input.csv> <config.json>\n"
            "  postgresql-to-csv: <config.json> <output.csv>\n");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        usage();
        return 2;
    }

    const char *cmd = argv[1];

    // normalize
    char cmd_l[64];
    snprintf(cmd_l, sizeof(cmd_l), "%s", cmd);
    for (char *p = cmd_l; *p; p++) *p = (char)tolower((unsigned char)*p);

    if (strcmp(cmd_l, "csv-to-postgresql") == 0) {
        return csv_to_postgresql(argv[2], argv[3]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd_l, "postgresql-to-csv") == 0) {
        return postgresql_to_csv(argv[2], argv[3]) == 0 ? 0 : 1;
    }

    usage();
    return 2;
}
