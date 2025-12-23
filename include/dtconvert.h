#ifndef DTCONVERT_H
#define DTCONVERT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <libgen.h>
#include <ctype.h>
#include <sys/stat.h>

// Version information
#define DTCONVERT_VERSION "1.0.0"

// Error codes
#define SUCCESS 0
#define ERR_INVALID_ARGS 1
#define ERR_FILE_NOT_FOUND 2
#define ERR_UNSUPPORTED_FORMAT 3
#define ERR_CONVERSION_FAILED 4
#define ERR_NO_CONVERTER 5

// Maximum path length
#define MAX_PATH_LEN 1024
#define MAX_FORMAT_LEN 16

// Document structure
typedef struct {
    char *path;
    char *filename;
    char *extension;
    char *full_path;
    bool exists;
    off_t size;
} Document;

// Conversion request structure
typedef struct {
    Document *input;
    char *input_format;
    char *output_format;
    char *output_path;
    bool overwrite;
    bool verbose;
} ConversionRequest;

// Function prototypes
// Document handling
Document* document_create(const char *path);
void document_destroy(Document *doc);
bool document_exists(const Document *doc);
char* document_get_extension(const char *filename);

// Conversion handling
int convert_document(ConversionRequest *request);
int find_converter(const char *from_format, const char *to_format);
int execute_converter(const char *converter_path, 
                      const char *input_path, 
                      const char *output_path);

// AI subcommand entrypoint
int ai_command(int argc, char **argv);

// Format utilities
bool is_supported_format(const char *format);
const char* get_format_description(const char *format);

// CLI interface
void print_usage(const char *program_name);
void print_version(void);
int parse_arguments(int argc, char *argv[], ConversionRequest *request);

// Utility functions
char* str_lower(char *str);
bool ends_with(const char *str, const char *suffix);
char* replace_extension(const char *filename, const char *new_ext);

#endif // DTCONVERT_H
