#include "../include/dtconvert.h"

#include <errno.h>
#include <fcntl.h>
#include <time.h>

static const char *env_or(const char *key, const char *fallback);

static void ai_usage(const char *program) {
    printf("Usage:\n");
    printf("  %s ai summarize <file> [-o <output.md>] [--backend ollama|openai] [--model <name>]\n", program);
    printf("  %s ai search <query> [--open] [--yes]\n", program);
    printf("  %s ai cite <url>... [--style apa|mla] [-o <citations.md>]\n", program);
    printf("\nBackends:\n");
    printf("  ollama (default): requires ollama server running. Env: DTCONVERT_OLLAMA_HOST (default http://127.0.0.1:11434)\n");
    printf("  openai: requires env OPENAI_API_KEY. Optional env OPENAI_BASE_URL (default https://api.openai.com/v1)\n");
}

static void die2(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
}

static char *read_all_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
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

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static bool command_exists(const char *name) {
    if (!name || !*name) return false;
    const char *path = getenv("PATH");
    if (!path) return false;

    const char *p = path;
    while (*p) {
        const char *sep = strchr(p, ':');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        char candidate[1024];
        if (len == 0 || len + 1 + strlen(name) + 1 >= sizeof(candidate)) {
            // skip empty or too-long PATH segment
        } else {
            memcpy(candidate, p, len);
            candidate[len] = '\0';
            strcat(candidate, "/");
            strcat(candidate, name);
            if (access(candidate, X_OK) == 0) return true;
        }

        if (!sep) break;
        p = sep + 1;
    }
    return false;
}

static int run_capture(char *const argv[], const char *stdin_path, char **out_buf) {
    if (!argv || !argv[0] || !out_buf) return -1;
    *out_buf = NULL;

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // child
        if (stdin_path) {
            int fd = open(stdin_path, O_RDONLY);
            if (fd >= 0) {
                (void)dup2(fd, STDIN_FILENO);
                close(fd);
            }
        }

        (void)dup2(pipefd[1], STDOUT_FILENO);
        (void)dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        execvp(argv[0], argv);
        _exit(127);
    }

    // parent
    close(pipefd[1]);

    size_t cap = 8192;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        close(pipefd[0]);
        (void)waitpid(pid, NULL, 0);
        return -1;
    }

    while (true) {
        if (len + 4096 + 1 > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                close(pipefd[0]);
                (void)waitpid(pid, NULL, 0);
                return -1;
            }
            buf = nb;
        }

        ssize_t n = read(pipefd[0], buf + len, cap - len - 1);
        if (n < 0) {
            free(buf);
            close(pipefd[0]);
            (void)waitpid(pid, NULL, 0);
            return -1;
        }
        if (n == 0) break;
        len += (size_t)n;
    }

    close(pipefd[0]);
    buf[len] = '\0';
    *out_buf = buf;

    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static char *json_escape_alloc(const char *s) {
    if (!s) return NULL;

    size_t n = strlen(s);
    // Worst case: every byte becomes two (e.g., '\\' -> '\\\\'), plus quotes handled by caller.
    char *out = (char *)malloc(n * 2 + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '"': out[j++] = '\\'; out[j++] = '"'; break;
            case '\n': out[j++] = '\\'; out[j++] = 'n'; break;
            case '\r': out[j++] = '\\'; out[j++] = 'r'; break;
            case '\t': out[j++] = '\\'; out[j++] = 't'; break;
            default:
                if (c < 0x20) {
                    // drop other control chars
                } else {
                    out[j++] = (char)c;
                }
                break;
        }
    }
    out[j] = '\0';
    return out;
}

static char *url_encode(const char *s) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(s);
    char *out = (char *)malloc(n * 3 + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = (char)c;
        } else {
            out[j++] = '%';
            out[j++] = hex[(c >> 4) & 0xF];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = '\0';
    return out;
}

static void json_unescape_inplace(char *s) {
    // minimal: handles \n, \r, \t, \", \\, ignores \uXXXX -> '?'
    if (!s) return;
    char *w = s;
    for (char *p = s; *p; p++) {
        if (*p != '\\') {
            *w++ = *p;
            continue;
        }
        p++;
        if (!*p) break;
        switch (*p) {
            case 'n': *w++ = '\n'; break;
            case 'r': *w++ = '\r'; break;
            case 't': *w++ = '\t'; break;
            case '"': *w++ = '"'; break;
            case '\\': *w++ = '\\'; break;
            case 'u':
                // skip 4 hex
                if (p[1] && p[2] && p[3] && p[4]) p += 4;
                *w++ = '?';
                break;
            default:
                *w++ = *p;
                break;
        }
    }
    *w = '\0';
}

static char *json_extract_string_value(const char *json, const char *key) {
    // naive but works for typical API JSON: finds "key":"..." and returns decoded string
    if (!json || !key) return NULL;

    char needle[256];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p += strlen(needle);

    // find ':'
    p = strchr(p, ':');
    if (!p) return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return NULL;
    p++;

    size_t cap = 256;
    size_t len = 0;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;

    bool esc = false;
    for (; *p; p++) {
        char c = *p;
        if (!esc) {
            if (c == '\\') {
                esc = true;
                if (len + 2 >= cap) {
                    cap *= 2;
                    out = (char *)realloc(out, cap);
                    if (!out) return NULL;
                }
                out[len++] = '\\';
                continue;
            }
            if (c == '"') break;
            if (len + 2 >= cap) {
                cap *= 2;
                out = (char *)realloc(out, cap);
                if (!out) return NULL;
            }
            out[len++] = c;
        } else {
            esc = false;
            if (len + 2 >= cap) {
                cap *= 2;
                out = (char *)realloc(out, cap);
                if (!out) return NULL;
            }
            out[len++] = c;
        }
    }

    out[len] = '\0';
    json_unescape_inplace(out);
    return out;
}

static char *json_extract_string_value_after(const char *json, const char *key, const char *start) {
    if (!json || !key) return NULL;
    const char *base = start ? start : json;

    char needle[256];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(base, needle);
    if (!p) return NULL;

    // reuse the main extractor from the located position
    return json_extract_string_value(p, key);
}

static int write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return 0;
}

static char *curl_get(const char *url) {
    if (!command_exists("curl")) return NULL;
    const char *timeout = env_or("DTCONVERT_AI_TIMEOUT", "20");
    char *out = NULL;
    char *const argv[] = {
        (char *)"curl",
        (char *)"-L",
        (char *)"-sS",
        (char *)"--connect-timeout",
        (char *)"5",
        (char *)"--max-time",
        (char *)timeout,
        (char *)url,
        NULL,
    };
    (void)run_capture(argv, NULL, &out);
    return out;
}

static char *write_temp_json_file(const char *json_body) {
    if (!json_body) return NULL;

    char tmpl[] = "/tmp/dtconvert_ai_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;

    size_t len = strlen(json_body);
    ssize_t written = write(fd, json_body, len);
    close(fd);

    if (written < 0 || (size_t)written != len) {
        unlink(tmpl);
        return NULL;
    }

    return strdup(tmpl);
}

static char *curl_post_json(const char *url, const char *json_body, const char *auth_bearer) {
    if (!command_exists("curl")) return NULL;
    const char *timeout = env_or("DTCONVERT_AI_TIMEOUT", "120");

    char *tmp_path = write_temp_json_file(json_body);
    if (!tmp_path) return NULL;

    char data_arg[1400];
    snprintf(data_arg, sizeof(data_arg), "@%s", tmp_path);

    char auth_hdr[1200];
    char *out = NULL;
    int rc;

    if (auth_bearer && auth_bearer[0] != '\0') {
        snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", auth_bearer);
        char *const argv[] = {
            (char *)"curl",
            (char *)"-sS",
            (char *)"--connect-timeout",
            (char *)"5",
            (char *)"--max-time",
            (char *)timeout,
            (char *)"-H",
            (char *)"Content-Type: application/json",
            (char *)"-H",
            auth_hdr,
            (char *)"--data-binary",
            data_arg,
            (char *)url,
            NULL,
        };
        rc = run_capture(argv, NULL, &out);
    } else {
        char *const argv[] = {
            (char *)"curl",
            (char *)"-sS",
            (char *)"--connect-timeout",
            (char *)"5",
            (char *)"--max-time",
            (char *)timeout,
            (char *)"-H",
            (char *)"Content-Type: application/json",
            (char *)"--data-binary",
            data_arg,
            (char *)url,
            NULL,
        };
        rc = run_capture(argv, NULL, &out);
    }

    (void)rc;
    unlink(tmp_path);
    free(tmp_path);
    return out;
}

static const char *env_or(const char *key, const char *fallback) {
    const char *v = getenv(key);
    return (v && v[0] != '\0') ? v : fallback;
}

static int ai_summarize(const char *input_path, const char *out_path, const char *backend, const char *model) {
    size_t n = 0;
    char *content = read_all_file(input_path, &n);
    if (!content) {
        fprintf(stderr, "Error: cannot read '%s': %s\n", input_path, strerror(errno));
        return ERR_FILE_NOT_FOUND;
    }

    // keep prompts bounded
    const size_t max_in = 60000;
    if (n > max_in) {
        content[max_in] = '\0';
        n = max_in;
    }

    char *response = NULL;

    if (strcmp(backend, "ollama") == 0) {
        const char *host = env_or("DTCONVERT_OLLAMA_HOST", "http://127.0.0.1:11434");
        char url[512];
        snprintf(url, sizeof(url), "%s/api/generate", host);

        // Build JSON body with fully JSON-escaped prompt.
        const char *prompt_prefix =
            "Summarize the following document. Use concise bullet points.\n\n---\n";

        size_t prompt_cap = strlen(prompt_prefix) + n + 1;
        char *prompt = (char *)malloc(prompt_cap);
        if (!prompt) {
            free(content);
            return ERR_CONVERSION_FAILED;
        }
        strcpy(prompt, prompt_prefix);
        strcat(prompt, content);

        char *prompt_json = json_escape_alloc(prompt);
        free(prompt);
        if (!prompt_json) {
            free(content);
            return ERR_CONVERSION_FAILED;
        }

        size_t body_cap = strlen(prompt_json) + strlen(model) + 128;
        char *body = (char *)malloc(body_cap);
        if (!body) {
            free(prompt_json);
            free(content);
            return ERR_CONVERSION_FAILED;
        }

        snprintf(body, body_cap,
                 "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false}",
                 model, prompt_json);

        free(prompt_json);

        char *raw = curl_post_json(url, body, NULL);
        free(body);
        if (!raw) {
            free(content);
            die2("AI summarize failed (curl required, and Ollama must be reachable)");
            return ERR_CONVERSION_FAILED;
        }

        // Ollama returns {"response":"...",...}
        response = json_extract_string_value(raw, "response");

        if (!response) {
            // Surface useful connectivity errors (captured from curl stderr)
            bool printed = false;
            if (strstr(raw, "Failed to connect") || strstr(raw, "Couldn't connect") || strstr(raw, "Connection refused")) {
                fprintf(stderr, "Error: Ollama is not reachable at %s\n", host);
                fprintf(stderr, "Hint: start it with `ollama serve` (and ensure a model is pulled, e.g. `ollama pull %s`)\n", model);
                printed = true;
            } else if (strstr(raw, "curl:") == raw) {
                fprintf(stderr, "%.*s\n", 400, raw);
                printed = true;
            }

            free(raw);
            if (printed) {
                free(content);
                return ERR_CONVERSION_FAILED;
            }
        } else {
            free(raw);
        }
    } else if (strcmp(backend, "openai") == 0) {
        const char *key = getenv("OPENAI_API_KEY");
        if (!key || key[0] == '\0') {
            free(content);
            die2("OPENAI_API_KEY is required for openai backend");
            return ERR_INVALID_ARGS;
        }
        const char *base = env_or("OPENAI_BASE_URL", "https://api.openai.com/v1");
        char url[512];
        snprintf(url, sizeof(url), "%s/chat/completions", base);

        const char *sys = "You are a helpful assistant.";
        const char *user_prefix = "Summarize the following document. Use concise bullet points.\n\n---\n";

        size_t user_cap = strlen(user_prefix) + n + 1;
        char *user_msg = (char *)malloc(user_cap);
        if (!user_msg) {
            free(content);
            return ERR_CONVERSION_FAILED;
        }
        strcpy(user_msg, user_prefix);
        strcat(user_msg, content);

        char *sys_json = json_escape_alloc(sys);
        char *user_json = json_escape_alloc(user_msg);
        free(user_msg);
        if (!sys_json || !user_json) {
            free(sys_json);
            free(user_json);
            free(content);
            return ERR_CONVERSION_FAILED;
        }

        size_t body_cap = strlen(sys_json) + strlen(user_json) + strlen(model) + 512;
        char *body = (char *)malloc(body_cap);
        if (!body) {
            free(sys_json);
            free(user_json);
            free(content);
            return ERR_CONVERSION_FAILED;
        }

        snprintf(body, body_cap,
                 "{"
                 "\"model\":\"%s\","
                 "\"messages\":["
                 "{\"role\":\"system\",\"content\":\"%s\"},"
                 "{\"role\":\"user\",\"content\":\"%s\"}"
                 "]"
                 "}",
                 model, sys_json, user_json);

        free(sys_json);
        free(user_json);

        char *raw = curl_post_json(url, body, key);
        free(body);
        if (!raw) {
            free(content);
            die2("AI summarize failed (curl required, and OpenAI endpoint must be reachable)");
            return ERR_CONVERSION_FAILED;
        }

        // Prefer extracting assistant content from choices[0].message.content.
        const char *choices = strstr(raw, "\"choices\"");
        response = json_extract_string_value_after(raw, "content", choices ? choices : raw);

        if (!response) {
            // If this is an OpenAI-style error payload, surface its message.
            const char *errp = strstr(raw, "\"error\"");
            char *errmsg = json_extract_string_value_after(raw, "message", errp ? errp : raw);
            if (errmsg) {
                fprintf(stderr, "Error: OpenAI API error: %s\n", errmsg);
                free(errmsg);
                free(raw);
                free(content);
                return ERR_CONVERSION_FAILED;
            }

            // Otherwise, show a short prefix to aid debugging.
            if (strstr(raw, "curl:") == raw) {
                fprintf(stderr, "%.*s\n", 400, raw);
            } else {
                fprintf(stderr, "Error: OpenAI returned an unexpected response (prefix): %.*s\n", 400, raw);
            }
            free(raw);
            free(content);
            return ERR_CONVERSION_FAILED;
        }
        free(raw);
    } else {
        free(content);
        fprintf(stderr, "Error: Unknown backend '%s'\n", backend);
        return ERR_INVALID_ARGS;
    }

    free(content);

    if (!response) {
        die2("AI backend returned an unexpected response (unable to extract text)");
        return ERR_CONVERSION_FAILED;
    }

    int rc = 0;
    if (out_path) {
        rc = write_text_file(out_path, response);
    } else {
        printf("%s\n", response);
    }

    free(response);
    return rc == 0 ? SUCCESS : ERR_CONVERSION_FAILED;
}

static int ai_search(int argc, char **argv, int start) {
    // args: <query> [--open] [--yes]
    if (start >= argc) {
        die2("ai search requires a query string");
        return ERR_INVALID_ARGS;
    }

    // Build query from all tokens until flags.
    int end = start;
    while (end < argc && strcmp(argv[end], "--open") != 0 && strcmp(argv[end], "--yes") != 0) {
        end++;
    }

    size_t qcap = 0;
    for (int i = start; i < end; i++) qcap += strlen(argv[i]) + 1;
    if (qcap == 0) {
        die2("ai search requires a query string");
        return ERR_INVALID_ARGS;
    }

    char *query = (char *)malloc(qcap + 1);
    if (!query) return ERR_CONVERSION_FAILED;
    query[0] = '\0';
    for (int i = start; i < end; i++) {
        if (i > start) strcat(query, " ");
        strcat(query, argv[i]);
    }

    bool open_browser = false;
    bool yes = false;

    for (int i = end; i < argc; i++) {
        if (strcmp(argv[i], "--open") == 0) {
            open_browser = true;
        } else if (strcmp(argv[i], "--yes") == 0) {
            yes = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            free(query);
            return ERR_INVALID_ARGS;
        } else {
            fprintf(stderr, "Error: Unknown argument: %s\n", argv[i]);
            free(query);
            return ERR_INVALID_ARGS;
        }
    }

    char *enc = url_encode(query);
    free(query);
    if (!enc) return ERR_CONVERSION_FAILED;

    char url[1024];
    // DuckDuckGo HTML endpoint is stable
    snprintf(url, sizeof(url), "https://duckduckgo.com/?q=%s", enc);
    free(enc);

    printf("%s\n", url);

    if (!open_browser) return SUCCESS;

    if (!command_exists("xdg-open")) {
        die2("xdg-open is required to open a browser tab");
        return ERR_CONVERSION_FAILED;
    }

    if (!yes) {
        printf("Open now? (y/N) ");
        fflush(stdout);
        char buf[16] = {0};
        if (!fgets(buf, sizeof(buf), stdin)) return SUCCESS;
        if (!(buf[0] == 'y' || buf[0] == 'Y')) return SUCCESS;
    }

    pid_t pid = fork();
    if (pid == 0) {
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
            (void)dup2(dn, STDOUT_FILENO);
            (void)dup2(dn, STDERR_FILENO);
            close(dn);
        }
        char *const xargv[] = {(char *)"xdg-open", url, NULL};
        execvp(xargv[0], xargv);
        _exit(127);
    }
    return SUCCESS;
}

static const char *strcasestr_simple(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;

    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        for (; i < nlen; i++) {
            unsigned char hc = (unsigned char)h[i];
            unsigned char nc = (unsigned char)needle[i];
            if (!hc) return NULL;
            if (tolower(hc) != tolower(nc)) break;
        }
        if (i == nlen) return h;
    }
    return NULL;
}

static char *html_extract_between(const char *html, const char *start_tag, const char *end_tag) {
    const char *a = strcasestr_simple(html, start_tag);
    if (!a) return NULL;
    a += strlen(start_tag);
    const char *b = strcasestr_simple(a, end_tag);
    if (!b) return NULL;
    size_t n = (size_t)(b - a);
    if (n == 0) return NULL;

    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, a, n);
    out[n] = '\0';

    // trim
    while (*out && isspace((unsigned char)*out)) memmove(out, out + 1, strlen(out));
    size_t m = strlen(out);
    while (m > 0 && isspace((unsigned char)out[m - 1])) out[--m] = '\0';

    return out;
}

static char *html_extract_meta(const char *html, const char *needle) {
    // needle like name=\"author\" or property=\"og:site_name\"
    const char *p = strcasestr_simple(html, needle);
    if (!p) return NULL;

    const char *c = strcasestr_simple(p, "content=");
    if (!c) return NULL;
    c += strlen("content=");
    while (*c && isspace((unsigned char)*c)) c++;

    if (*c == '\'' || *c == '"') {
        char q = *c++;
        const char *e = strchr(c, q);
        if (!e) return NULL;
        size_t n = (size_t)(e - c);
        char *out = (char *)malloc(n + 1);
        if (!out) return NULL;
        memcpy(out, c, n);
        out[n] = '\0';
        return out;
    }

    // unquoted: read until space or '>'
    const char *e = c;
    while (*e && !isspace((unsigned char)*e) && *e != '>') e++;
    size_t n = (size_t)(e - c);
    if (n == 0) return NULL;
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, c, n);
    out[n] = '\0';
    return out;
}

static void today_ymd(char out[32]) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, 32, "%Y-%m-%d", &tmv);
}

static int ai_cite_urls(int argc, char **argv, int start) {
    const char *style = "apa";
    const char *out_path = NULL;

    // parse options after urls too (simple): --style X, -o PATH
    // gather urls first
    int url_end = start;
    while (url_end < argc && strncmp(argv[url_end], "--", 2) != 0 && strcmp(argv[url_end], "-o") != 0) {
        url_end++;
    }

    if (url_end == start) {
        die2("ai cite requires one or more URLs");
        return ERR_INVALID_ARGS;
    }

    for (int i = url_end; i < argc; i++) {
        if (strcmp(argv[i], "--style") == 0) {
            if (i + 1 >= argc) return ERR_INVALID_ARGS;
            style = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) return ERR_INVALID_ARGS;
            out_path = argv[i + 1];
            i++;
        } else {
            fprintf(stderr, "Error: Unknown argument: %s\n", argv[i]);
            return ERR_INVALID_ARGS;
        }
    }

    if (strcmp(style, "apa") != 0 && strcmp(style, "mla") != 0) {
        fprintf(stderr, "Error: Unsupported style '%s' (use apa or mla)\n", style);
        return ERR_INVALID_ARGS;
    }

    char accessed[32];
    today_ymd(accessed);

    size_t out_cap = 4096;
    size_t out_len = 0;
    char *out = (char *)malloc(out_cap);
    if (!out) return ERR_CONVERSION_FAILED;
    out[0] = '\0';

    for (int i = start; i < url_end; i++) {
        const char *url = argv[i];
        char *html = curl_get(url);

        char *title = NULL;
        char *author = NULL;
        char *date = NULL;
        char *site = NULL;

        if (html) {
            title = html_extract_between(html, "<title>", "</title>");
            if (!title) title = html_extract_meta(html, "property=\"og:title\"");
            if (!title) title = html_extract_meta(html, "name=\"title\"");

            author = html_extract_meta(html, "name=\"author\"");
            if (!author) author = html_extract_meta(html, "property=\"article:author\"");

            date = html_extract_meta(html, "property=\"article:published_time\"");
            if (!date) date = html_extract_meta(html, "name=\"date\"");
            if (!date) date = html_extract_meta(html, "property=\"og:published_time\"");

            site = html_extract_meta(html, "property=\"og:site_name\"");
        }

        if (!title) title = strdup("(no title found)");
        if (!author) author = strdup("");
        if (!date) date = strdup("n.d.");
        if (!site) site = strdup("");

        char line[4096];
        if (strcmp(style, "apa") == 0) {
            // Minimal APA-ish
            if (author[0] != '\0') {
                snprintf(line, sizeof(line), "%s (%s). %s. %s. %s (accessed %s).\n",
                         author, date, title, site, url, accessed);
            } else {
                snprintf(line, sizeof(line), "%s (%s). %s. %s (accessed %s).\n",
                         title, date, site[0] ? site : url, url, accessed);
            }
        } else {
            // Minimal MLA-ish
            if (author[0] != '\0') {
                snprintf(line, sizeof(line), "%s. \"%s.\" %s, %s, %s. Accessed %s.\n",
                         author, title, site[0] ? site : "", date, url, accessed);
            } else {
                snprintf(line, sizeof(line), "\"%s.\" %s, %s. Accessed %s.\n",
                         title, site[0] ? site : "", url, accessed);
            }
        }

        size_t need = strlen(line);
        if (out_len + need + 1 > out_cap) {
            while (out_len + need + 1 > out_cap) out_cap *= 2;
            char *nb = (char *)realloc(out, out_cap);
            if (!nb) {
                free(out);
                free(html);
                free(title);
                free(author);
                free(date);
                free(site);
                return ERR_CONVERSION_FAILED;
            }
            out = nb;
        }
        memcpy(out + out_len, line, need);
        out_len += need;
        out[out_len] = '\0';

        free(html);
        free(title);
        free(author);
        free(date);
        free(site);
    }

    int rc = 0;
    if (out_path) {
        rc = write_text_file(out_path, out);
    } else {
        printf("%s", out);
    }

    free(out);
    return rc == 0 ? SUCCESS : ERR_CONVERSION_FAILED;
}

int ai_command(int argc, char **argv) {
    // argv[0]=program, argv[1]="ai", argv[2]=subcommand
    if (argc < 3) {
        ai_usage(argv[0]);
        return ERR_INVALID_ARGS;
    }

    // global help/version
    for (int j = 1; j < argc; j++) {
        if (strcmp(argv[j], "-h") == 0 || strcmp(argv[j], "--help") == 0) {
            ai_usage(argv[0]);
            return SUCCESS;
        }
        if (strcmp(argv[j], "--version") == 0) {
            print_version();
            return SUCCESS;
        }
    }

    const char *sub = argv[2];

    if (strcmp(sub, "search") == 0) {
        return ai_search(argc, argv, 3);
    }

    if (strcmp(sub, "cite") == 0) {
        return ai_cite_urls(argc, argv, 3);
    }

    if (strcmp(sub, "summarize") == 0) {
        if (argc < 4) {
            die2("ai summarize requires a file path");
            return ERR_INVALID_ARGS;
        }

        const char *input = argv[3];
        const char *out = NULL;
        const char *backend = "ollama";
        const char *model = NULL;

        // defaults
        const char *ollama_model = env_or("DTCONVERT_OLLAMA_MODEL", "llama3.1");
        const char *openai_model = env_or("DTCONVERT_OPENAI_MODEL", "gpt-4o-mini");
        model = ollama_model;

        for (int i = 4; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
                if (i + 1 >= argc) return ERR_INVALID_ARGS;
                out = argv[i + 1];
                i++;
            } else if (strcmp(argv[i], "--backend") == 0) {
                if (i + 1 >= argc) return ERR_INVALID_ARGS;
                backend = argv[i + 1];
                i++;
                if (strcmp(backend, "openai") == 0) model = openai_model;
                if (strcmp(backend, "ollama") == 0) model = ollama_model;
            } else if (strcmp(argv[i], "--model") == 0) {
                if (i + 1 >= argc) return ERR_INVALID_ARGS;
                model = argv[i + 1];
                i++;
            } else {
                fprintf(stderr, "Error: Unknown argument: %s\n", argv[i]);
                return ERR_INVALID_ARGS;
            }
        }

        return ai_summarize(input, out, backend, model);
    }

    fprintf(stderr, "Error: Unknown ai subcommand: %s\n", sub);
    ai_usage(argv[0]);
    return ERR_INVALID_ARGS;
}
