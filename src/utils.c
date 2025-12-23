#include "../include/dtconvert.h"

void print_usage(const char *program_name) {
    printf("dtconvert v%s\n", DTCONVERT_VERSION);
    printf("Usage:\n");
    printf("  %s <document> --to <format> [options]\n", program_name);
    printf("  %s ai <summarize|search|cite> ...\n", program_name);
    printf("\nOptions:\n");
    printf("  --from FORMAT         Override detected input format (e.g., postgresql)\n");
    printf("  --to FORMAT           Target format (pdf, docx, txt, etc.)\n");
    printf("  -o, --output FILE     Output file path\n");
    printf("                        For DB targets (e.g., postgresql), this is a JSON config file path\n");
    printf("  -f, --force           Overwrite existing output file\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help message\n");
    printf("  --version             Show version information\n");
    printf("\nExamples:\n");
    printf("  %s document.docx --to pdf\n", program_name);
    printf("  %s /path/to/file.odt --to pdf -o output.pdf\n", program_name);
    printf("  %s spreadsheet.xlsx --to csv --verbose\n", program_name);
    printf("  %s people.csv --to postgresql -o examples/postgresql.csv_to_postgresql.json\n", program_name);
    printf("  %s examples/postgresql.csv_to_postgresql.json --from postgresql --to csv -o export.csv\n", program_name);
    printf("  %s ai search \"postgresql copy csv\" --open\n", program_name);
}

void print_version(void) {
    printf("dtconvert version %s\n", DTCONVERT_VERSION);
    printf("A modular document conversion utility\n");
}

int parse_arguments(int argc, char *argv[], ConversionRequest *request) {
    if (!request) {
        return ERR_INVALID_ARGS;
    }

    // Defaults
    request->input = NULL;
    request->input_format = NULL;
    request->output_format = NULL;
    request->output_path = NULL;
    request->overwrite = false;
    request->verbose = false;

    // Global flags that should work in any position
    for (int j = 1; j < argc; j++) {
        if (strcmp(argv[j], "-h") == 0 || strcmp(argv[j], "--help") == 0) {
            print_usage(argv[0]);
            return SUCCESS;
        }
        if (strcmp(argv[j], "--version") == 0) {
            print_version();
            return SUCCESS;
        }
    }

    if (argc < 3) {
        print_usage(argv[0]);
        return ERR_INVALID_ARGS;
    }
    
    // Initialize request input holder (path only; main() will create full Document)
    request->input = malloc(sizeof(Document));
    if (!request->input) {
        return ERR_CONVERSION_FAILED;
    }
    memset(request->input, 0, sizeof(Document));
    
    // Get document path (first non-option argument)
    int i = 1;
    while (i < argc && argv[i][0] == '-') i++;
    
    if (i >= argc) {
        fprintf(stderr, "Error: No document specified\n");
        free(request->input);
        request->input = NULL;
        return ERR_INVALID_ARGS;
    }
    
    request->input->path = strdup(argv[i]);
    if (!request->input->path) {
        free(request->input);
        request->input = NULL;
        return ERR_CONVERSION_FAILED;
    }
    i++;
    
    // Parse remaining arguments
    while (i < argc) {
        if (strcmp(argv[i], "--from") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing format after --from\n");
                free(request->input->path);
                free(request->input);
                request->input = NULL;
                return ERR_INVALID_ARGS;
            }

            free(request->input_format);
            request->input_format = strdup(argv[i + 1]);
            if (!request->input_format) {
                free(request->input->path);
                free(request->input);
                request->input = NULL;
                return ERR_CONVERSION_FAILED;
            }
            str_lower(request->input_format);

            // Common aliases
            if (strcmp(request->input_format, "excel") == 0) {
                free(request->input_format);
                request->input_format = strdup("xlsx");
                if (!request->input_format) {
                    free(request->input->path);
                    free(request->input);
                    request->input = NULL;
                    return ERR_CONVERSION_FAILED;
                }
            }

            if (strcmp(request->input_format, "pg") == 0 ||
                strcmp(request->input_format, "postgres") == 0 ||
                strcmp(request->input_format, "postgresql") == 0) {
                free(request->input_format);
                request->input_format = strdup("postgresql");
                if (!request->input_format) {
                    free(request->input->path);
                    free(request->input);
                    request->input = NULL;
                    return ERR_CONVERSION_FAILED;
                }
            }
            i += 2;
        } else if (strcmp(argv[i], "--to") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing format after --to\n");
                free(request->input->path);
                free(request->input);
                request->input = NULL;
                free(request->input_format);
                request->input_format = NULL;
                return ERR_INVALID_ARGS;
            }
            request->output_format = strdup(argv[i + 1]);
            if (!request->output_format) {
                free(request->input->path);
                free(request->input);
                request->input = NULL;
                free(request->input_format);
                request->input_format = NULL;
                return ERR_CONVERSION_FAILED;
            }
            str_lower(request->output_format);

            // Common aliases
            if (strcmp(request->output_format, "excel") == 0) {
                free(request->output_format);
                request->output_format = strdup("xlsx");
                if (!request->output_format) {
                    free(request->input->path);
                    free(request->input);
                    request->input = NULL;
                    free(request->input_format);
                    request->input_format = NULL;
                    return ERR_CONVERSION_FAILED;
                }
            }

            if (strcmp(request->output_format, "pg") == 0 ||
                strcmp(request->output_format, "postgres") == 0 ||
                strcmp(request->output_format, "postgresql") == 0) {
                free(request->output_format);
                request->output_format = strdup("postgresql");
                if (!request->output_format) {
                    free(request->input->path);
                    free(request->input);
                    request->input = NULL;
                    free(request->input_format);
                    request->input_format = NULL;
                    return ERR_CONVERSION_FAILED;
                }
            }
            i += 2;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing filename after %s\n", argv[i]);
                free(request->input->path);
                free(request->input);
                request->input = NULL;
                free(request->input_format);
                request->input_format = NULL;
                free(request->output_format);
                request->output_format = NULL;
                return ERR_INVALID_ARGS;
            }
            request->output_path = strdup(argv[i + 1]);
            if (!request->output_path) {
                free(request->input->path);
                free(request->input);
                request->input = NULL;
                free(request->input_format);
                request->input_format = NULL;
                free(request->output_format);
                request->output_format = NULL;
                return ERR_CONVERSION_FAILED;
            }
            i += 2;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            request->overwrite = true;
            i++;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            request->verbose = true;
            i++;
        } else {
            fprintf(stderr, "Error: Unknown argument: %s\n", argv[i]);
            free(request->input->path);
            free(request->input);
            request->input = NULL;
            free(request->input_format);
            request->input_format = NULL;
            free(request->output_format);
            request->output_format = NULL;
            free(request->output_path);
            request->output_path = NULL;
            return ERR_INVALID_ARGS;
        }
    }
    
    // Validate required arguments
    if (request->output_format == NULL) {
        fprintf(stderr, "Error: Output format not specified (use --to)\n");
        free(request->input->path);
        free(request->input);
        request->input = NULL;
        free(request->input_format);
        request->input_format = NULL;
        free(request->output_path);
        request->output_path = NULL;
        return ERR_INVALID_ARGS;
    }
    
    return SUCCESS;
}

char* str_lower(char *str) {
    if (!str) return NULL;
    for (char *p = str; *p; p++) {
        *p = tolower(*p);
    }
    return str;
}

bool ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return false;
    
    return strncmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

char* replace_extension(const char *filename, const char *new_ext) {
    if (!filename || !new_ext) return NULL;
    
    // Find the last dot
    const char *last_dot = strrchr(filename, '.');
    const char *last_slash = strrchr(filename, '/');
    
    // If there's no dot, or dot is before the last slash, append extension
    if (!last_dot || (last_slash && last_dot < last_slash)) {
        char *result = malloc(strlen(filename) + strlen(new_ext) + 2);
        if (result) {
            sprintf(result, "%s.%s", filename, new_ext);
        }
        return result;
    }
    
    // Replace extension
    size_t base_len = last_dot - filename;
    char *result = malloc(base_len + strlen(new_ext) + 2);
    if (result) {
        strncpy(result, filename, base_len);
        result[base_len] = '\0';
        strcat(result, ".");
        strcat(result, new_ext);
    }
    return result;
}