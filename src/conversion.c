#include "../include/dtconvert.h"
#include <fcntl.h>
#include <errno.h>

// Converter registry structure
typedef struct {
    char *from_format;
    char *to_format;
    char *converter_path;
    char *description;
} Converter;

// Built-in converter registry
static Converter converters[] = {
    {"docx", "pdf", "modules/docx_to_pdf.sh", "DOCX to PDF converter"},
    {"docx", "odt", "modules/docx_to_odt.sh", "DOCX to ODT converter"},
    {"odt", "pdf", "modules/odt_to_pdf.sh", "ODT to PDF converter"},
    {"odt", "docx", "modules/odt_to_docx.sh", "ODT to DOCX converter"},
    {"txt", "pdf", "modules/txt_to_pdf.sh", "Text to PDF converter"},
    {"csv", "txt", "modules/csv_to_txt.sh", "CSV to Text converter"},
    {"csv", "pdf", "modules/csv_to_pdf.sh", "CSV to PDF converter"},
    {"csv", "xlsx", "modules/csv_to_xlsx.sh", "CSV to XLSX converter"},
    {"xlsx", "csv", "modules/xlsx_to_csv.sh", "XLSX to CSV converter"},
    {"csv", "json", "lib/converters/data_convert", "CSV to JSON converter"},
    {"json", "csv", "lib/converters/data_convert", "JSON to CSV converter"},
    {"json", "yaml", "lib/converters/data_convert", "JSON to YAML converter"},
    {"yaml", "json", "lib/converters/data_convert", "YAML to JSON converter"},
    {"csv", "yaml", "lib/converters/data_convert", "CSV to YAML converter"},
    {"yaml", "csv", "lib/converters/data_convert", "YAML to CSV converter"},
    {"csv", "sql", "modules/csv_to_sql.sh", "CSV to SQL converter"},
    {"sql", "csv", "modules/sql_to_csv.sh", "SQL to CSV converter"},
    {"txt", "tokens", "modules/txt_to_tokens.sh", "Text to tokens converter"},
    {"csv", "postgresql", "modules/csv_to_postgresql.sh", "CSV to PostgreSQL importer"},
    {"postgresql", "csv", "modules/postgresql_to_csv.sh", "PostgreSQL to CSV exporter"},
    {NULL, NULL, NULL, NULL}  // Sentinel
};

static bool is_storage_format(const char *format) {
    return (format && strcmp(format, "postgresql") == 0);
}

static char *make_temp_with_ext(const char *ext) {
    char tmpl[] = "/tmp/dtconvertXXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;
    close(fd);

    size_t out_len = strlen(tmpl) + 1 + (ext ? strlen(ext) : 0) + 1;
    char *out = (char *)malloc(out_len);
    if (!out) {
        unlink(tmpl);
        return NULL;
    }
    if (ext && ext[0] != '\0') {
        snprintf(out, out_len, "%s.%s", tmpl, ext);
    } else {
        snprintf(out, out_len, "%s", tmpl);
    }

    if (rename(tmpl, out) != 0) {
        unlink(tmpl);
        free(out);
        return NULL;
    }
    return out;
}

typedef struct {
    const char *name;
    int prev;
    int prev_converter;
    bool seen;
} Node;

static int format_index(Node *nodes, int n, const char *name) {
    for (int i = 0; i < n; i++) {
        if (nodes[i].name && strcmp(nodes[i].name, name) == 0) return i;
    }
    return -1;
}

static int build_nodes(Node *nodes, int max_nodes) {
    int n = 0;
    for (int i = 0; converters[i].from_format != NULL; i++) {
        const char *a = converters[i].from_format;
        const char *b = converters[i].to_format;

        if (format_index(nodes, n, a) < 0 && n < max_nodes) {
            nodes[n++] = (Node){.name = a, .prev = -1, .prev_converter = -1, .seen = false};
        }
        if (format_index(nodes, n, b) < 0 && n < max_nodes) {
            nodes[n++] = (Node){.name = b, .prev = -1, .prev_converter = -1, .seen = false};
        }
    }
    return n;
}

static int find_path(const char *from, const char *to, int *out_converters, int max_steps) {
    Node nodes[64] = {0};
    int n = build_nodes(nodes, 64);

    int start = format_index(nodes, n, from);
    int goal = format_index(nodes, n, to);
    if (start < 0 || goal < 0) return -1;

    int q[64];
    int qh = 0, qt = 0;
    nodes[start].seen = true;
    q[qt++] = start;

    while (qh < qt) {
        int u = q[qh++];
        if (u == goal) break;

        for (int cid = 0; converters[cid].from_format != NULL; cid++) {
            if (strcmp(converters[cid].from_format, nodes[u].name) != 0) continue;

            // Avoid routing INTO a storage format unless it's the final target.
            if (is_storage_format(converters[cid].to_format) && strcmp(to, "postgresql") != 0) continue;

            int v = format_index(nodes, n, converters[cid].to_format);
            if (v < 0) continue;
            if (nodes[v].seen) continue;

            nodes[v].seen = true;
            nodes[v].prev = u;
            nodes[v].prev_converter = cid;
            q[qt++] = v;
            if (qt >= 64) break;
        }
    }

    if (!nodes[goal].seen) return -1;

    int tmp[64];
    int steps = 0;
    for (int cur = goal; cur != start; cur = nodes[cur].prev) {
        if (steps >= 64) break;
        tmp[steps++] = nodes[cur].prev_converter;
    }
    if (steps <= 0 || steps > max_steps) return -1;

    for (int i = 0; i < steps; i++) {
        out_converters[i] = tmp[steps - 1 - i];
    }
    return steps;
}

static int execute_pipeline(ConversionRequest *request, const char *from_format) {
    int steps_ids[64];
    int steps = find_path(from_format, request->output_format, steps_ids, 64);
    if (steps < 0) {
        fprintf(stderr, "Error: No converter found for %s -> %s\n", from_format, request->output_format);
        return ERR_NO_CONVERTER;
    }

    const char *current_input = request->input->full_path;
    char *temp_paths[64] = {0};
    int temp_count = 0;

    for (int s = 0; s < steps; s++) {
        int cid = steps_ids[s];
        const char *to_fmt = converters[cid].to_format;

        const char *step_output = NULL;
        char *tmp = NULL;

        bool last = (s == steps - 1);
        if (last) {
            step_output = request->output_path;
        } else {
            if (is_storage_format(to_fmt)) {
                fprintf(stderr, "Error: Cannot pipeline through storage target '%s'\n", to_fmt);
                for (int k = 0; k < temp_count; k++) {
                    unlink(temp_paths[k]);
                    free(temp_paths[k]);
                }
                return ERR_CONVERSION_FAILED;
            }
            tmp = make_temp_with_ext(to_fmt);
            if (!tmp) {
                fprintf(stderr, "Error: Failed to create temp file\n");
                for (int k = 0; k < temp_count; k++) {
                    unlink(temp_paths[k]);
                    free(temp_paths[k]);
                }
                return ERR_CONVERSION_FAILED;
            }
            temp_paths[temp_count++] = tmp;
            step_output = tmp;
        }

        int rc = execute_converter(converters[cid].converter_path, current_input, step_output);
        if (rc != 0) {
            fprintf(stderr, "Error: Converter failed with code %d\n", rc);
            for (int k = 0; k < temp_count; k++) {
                unlink(temp_paths[k]);
                free(temp_paths[k]);
            }
            return ERR_CONVERSION_FAILED;
        }

        current_input = step_output;
    }

    for (int k = 0; k < temp_count; k++) {
        unlink(temp_paths[k]);
        free(temp_paths[k]);
    }
    return SUCCESS;
}

int convert_document(ConversionRequest *request) {
    if (!request || !request->input || !request->output_format) {
        return ERR_INVALID_ARGS;
    }

    // Storage targets (e.g., postgresql) use output_path as a config file path.
    bool output_is_config = is_storage_format(request->output_format);
    
    const char *from_format = (request->input_format && request->input_format[0] != '\0')
                                 ? request->input_format
                                 : request->input->extension;

    if (!request->output_path) {
        fprintf(stderr, "Error: Missing -o/--output argument\n");
        return ERR_INVALID_ARGS;
    }

    if (output_is_config) {
        if (access(request->output_path, R_OK) != 0) {
            fprintf(stderr, "Error: Config file not found or not readable: %s\n", request->output_path);
            return ERR_FILE_NOT_FOUND;
        }
    } else {
        // Check if output file exists
        if (!request->overwrite && access(request->output_path, F_OK) == 0) {
            fprintf(stderr, "Error: Output file '%s' already exists. Use -f to overwrite.\n",
                    request->output_path);
            return ERR_CONVERSION_FAILED;
        }
    }

    // Try direct converter first
    int converter_id = find_converter(from_format, request->output_format);
    if (converter_id >= 0) {
        int result = execute_converter(converters[converter_id].converter_path,
                                       request->input->full_path,
                                       request->output_path);

        if (result != 0) {
            fprintf(stderr, "Error: Converter failed with code %d\n", result);
            return ERR_CONVERSION_FAILED;
        }

        if (request->verbose) {
            printf("Converter executed: %s\n", converters[converter_id].description);
        }
        return SUCCESS;
    }

    // Pipeline fallback (e.g., postgresql -> csv -> json)
    if (request->verbose) {
        printf("No direct converter for %s -> %s; attempting pipeline...\n", from_format, request->output_format);
    }
    return execute_pipeline(request, from_format);
}

int find_converter(const char *from_format, const char *to_format) {
    if (!from_format || !to_format) return -1;
    
    for (int i = 0; converters[i].from_format != NULL; i++) {
        if (strcmp(converters[i].from_format, from_format) == 0 &&
            strcmp(converters[i].to_format, to_format) == 0) {
            return i;
        }
    }
    
    return -1;
}

static bool path_is_absolute(const char *p) {
    return p && p[0] == '/';
}

static char *xstrdup0(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)malloc(n);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

static char *path_join2(const char *a, const char *b) {
    if (!a || !b) return NULL;
    size_t na = strlen(a);
    size_t nb = strlen(b);
    bool need_slash = (na > 0 && a[na - 1] != '/');
    size_t n = na + (need_slash ? 1 : 0) + nb + 1;
    char *out = (char *)malloc(n);
    if (!out) return NULL;
    snprintf(out, n, "%s%s%s", a, need_slash ? "/" : "", b);
    return out;
}

static char *path_join3(const char *a, const char *b, const char *c) {
    if (!a || !b || !c) return NULL;
    char *ab = path_join2(a, b);
    if (!ab) return NULL;
    char *abc = path_join2(ab, c);
    free(ab);
    return abc;
}

static char *exe_dir_parent(void) {
    // Returns parent dir of the executable directory.
    // Example: /home/user/project/bin/dtconvert -> /home/user/project
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return NULL;
    buf[n] = '\0';

    // dirname() may modify its argument.
    char tmp1[4096];
    snprintf(tmp1, sizeof(tmp1), "%s", buf);
    char *d1 = dirname(tmp1); // .../bin
    if (!d1) return NULL;

    char tmp2[4096];
    snprintf(tmp2, sizeof(tmp2), "%s", d1);
    char *d2 = dirname(tmp2); // .../
    if (!d2) return NULL;

    return xstrdup0(d2);
}

static char *resolve_converter_path(const char *converter_path) {
    if (!converter_path) return NULL;
    if (path_is_absolute(converter_path)) return xstrdup0(converter_path);

    const char *home = getenv("DTCONVERT_HOME");
    if (home && home[0] != '\0') {
        return path_join2(home, converter_path);
    }

    // Prefer resolving relative to the executable location to avoid cwd-based hijacking.
    char *base = exe_dir_parent();
    if (base) {
        char *p = path_join2(base, converter_path);
        free(base);
        return p;
    }

    // Fallback (last resort): keep original relative path.
    return xstrdup0(converter_path);
}

static char *resolve_converter_path_with_fallbacks(const char *converter_path) {
    if (!converter_path) return NULL;

    // 1) Direct resolution: dev tree (repo root + modules/..., lib/converters/...)
    char *p = resolve_converter_path(converter_path);
    if (p && access(p, X_OK) == 0) return p;
    free(p);

    // 2) If DTCONVERT_HOME is set, support both:
    //    - DTCONVERT_HOME=<repo root> (dev tree)
    //    - DTCONVERT_HOME=<prefix>/lib/dtconvert (installed tree root)
    //    - DTCONVERT_HOME=<prefix> (installed prefix)
    const char *home = getenv("DTCONVERT_HOME");
    if (home && home[0] != '\0') {
        if (strncmp(converter_path, "modules/", 8) == 0) {
            const char *leaf = converter_path + 8;
            // installed-tree root
            char *p1 = path_join3(home, "converters", leaf);
            if (p1 && access(p1, X_OK) == 0) return p1;
            free(p1);
            // prefix root
            char *p2 = path_join3(home, "lib/dtconvert/converters", leaf);
            if (p2 && access(p2, X_OK) == 0) return p2;
            free(p2);
        } else if (strncmp(converter_path, "lib/converters/", 15) == 0) {
            const char *leaf = converter_path + 15;
            // installed-tree root
            char *p1 = path_join3(home, "lib/converters", leaf);
            if (p1 && access(p1, X_OK) == 0) return p1;
            free(p1);
            // prefix root
            char *p2 = path_join3(home, "lib/dtconvert/lib/converters", leaf);
            if (p2 && access(p2, X_OK) == 0) return p2;
            free(p2);
        }
    }

    // 3) Support installed layout relative to the executable's prefix.
    //    If dtconvert is installed to <prefix>/bin/dtconvert, then:
    //    - modules are at <prefix>/lib/dtconvert/converters/
    //    - helper binaries are at <prefix>/lib/dtconvert/lib/converters/
    char *prefix = exe_dir_parent();
    if (prefix) {
        if (strncmp(converter_path, "modules/", 8) == 0) {
            const char *leaf = converter_path + 8;
            char *p1 = path_join3(prefix, "lib/dtconvert/converters", leaf);
            if (p1 && access(p1, X_OK) == 0) {
                free(prefix);
                return p1;
            }
            free(p1);
        } else if (strncmp(converter_path, "lib/converters/", 15) == 0) {
            const char *leaf = converter_path + 15;
            char *p1 = path_join3(prefix, "lib/dtconvert/lib/converters", leaf);
            if (p1 && access(p1, X_OK) == 0) {
                free(prefix);
                return p1;
            }
            free(p1);
        }
        free(prefix);
    }

    // System-wide install fallbacks.
    // Makefile installs modules to /usr/local/lib/dtconvert/converters/ and helpers to /usr/local/lib/dtconvert/lib/converters/.
    const char *local_root = "/usr/local/lib/dtconvert";
    const char *usr_root = "/usr/lib/dtconvert";

    if (converter_path && strncmp(converter_path, "modules/", 8) == 0) {
        const char *leaf = converter_path + 8;
        char *p1 = path_join2("/usr/local/lib/dtconvert/converters", leaf);
        if (p1 && access(p1, X_OK) == 0) return p1;
        free(p1);
        char *p2 = path_join2("/usr/lib/dtconvert/converters", leaf);
        if (p2 && access(p2, X_OK) == 0) return p2;
        free(p2);
    }

    if (converter_path && strncmp(converter_path, "lib/converters/", 15) == 0) {
        char *p1 = path_join2(local_root, converter_path);
        if (p1 && access(p1, X_OK) == 0) return p1;
        free(p1);
        char *p2 = path_join2(usr_root, converter_path);
        if (p2 && access(p2, X_OK) == 0) return p2;
        free(p2);
    }

    // Give up.
    return NULL;
}

int execute_converter(const char *converter_path, const char *input_path, const char *output_path) {
    if (!converter_path || !input_path || !output_path) {
        return -1;
    }

    char *resolved = resolve_converter_path_with_fallbacks(converter_path);
    if (!resolved) {
        fprintf(stderr, "Error: Converter not found or not executable: %s\n", converter_path);
        return -1;
    }
    
    // Fork and execute converter
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        execl(resolved, resolved, input_path, output_path, NULL);
        
        // If execl returns, there was an error
        fprintf(stderr, "Error: Failed to execute converter: %s: %s\n", resolved, strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        // Parent process
        int status;
        (void)waitpid(pid, &status, 0);
        free(resolved);
        
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else {
            return -1;
        }
    } else {
        // Fork failed
        fprintf(stderr, "Error: Failed to fork process\n");
        free(resolved);
        return -1;
    }
}