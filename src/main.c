#include "../include/dtconvert.h"

int main(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "ai") == 0) {
        return ai_command(argc, argv);
    }

    ConversionRequest request = {0};
    
    // Parse command line arguments
    int parse_result = parse_arguments(argc, argv, &request);
    if (parse_result != SUCCESS) {
        return parse_result;
    }

    // parse_arguments can fully handle commands like --help/--version.
    // In that case it returns SUCCESS but does not populate a conversion request.
    if (request.input == NULL) {
        free(request.input_format);
        request.input_format = NULL;
        return SUCCESS;
    }

    Document *arg_doc = request.input;
    const char *input_path = (arg_doc && arg_doc->path) ? arg_doc->path : NULL;
    if (!input_path) {
        free(arg_doc);
        free(request.input_format);
        free(request.output_format);
        free(request.output_path);
        return ERR_INVALID_ARGS;
    }
    
    // Create document object
    Document *doc = document_create(input_path);
    if (!doc) {
        fprintf(stderr, "Error: Failed to create document object\n");
        free(arg_doc->path);
        free(arg_doc);
        free(request.input_format);
        free(request.output_format);
        free(request.output_path);
        return ERR_FILE_NOT_FOUND;
    }

    free(arg_doc->path);
    free(arg_doc);
    
    if (!document_exists(doc)) {
        fprintf(stderr, "Error: File '%s' does not exist\n", doc->path);
        document_destroy(doc);
        free(request.input_format);
        free(request.output_format);
        free(request.output_path);
        return ERR_FILE_NOT_FOUND;
    }
    
    request.input = doc;

    if (strcmp(request.output_format, "postgresql") == 0 && request.output_path == NULL) {
        fprintf(stderr, "Error: PostgreSQL target requires -o <config.json>\n");
        document_destroy(doc);
        free(request.input_format);
        free(request.output_format);
        return ERR_INVALID_ARGS;
    }
    
    // Generate output path if not specified
    if (request.output_path == NULL) {
        char base_path[MAX_PATH_LEN];
        snprintf(base_path, sizeof(base_path), "%s", doc->full_path);
        char *dot = strrchr(base_path, '.');
        if (dot) *dot = '\0';
        
        request.output_path = malloc(strlen(base_path) + strlen(request.output_format) + 2);
        if (request.output_path) {
            snprintf(request.output_path, strlen(base_path) + strlen(request.output_format) + 2,
                     "%s.%s", base_path, request.output_format);
        } else {
            fprintf(stderr, "Error: Memory allocation failed\n");
            document_destroy(doc);
            free(request.input_format);
            free(request.output_format);
            return ERR_CONVERSION_FAILED;
        }
    }
    
    if (request.verbose) {
        printf("Converting: %s -> %s\n", doc->path, request.output_path);
        printf("Input format: %s\n", request.input_format ? request.input_format : doc->extension);
        printf("Output format: %s\n", request.output_format);
    }
    
    // Perform conversion
    int result = convert_document(&request);
    
    if (result == SUCCESS) {
        if (request.verbose) {
            printf("Conversion successful!\n");
        }
    } else {
        fprintf(stderr, "Conversion failed with error code: %d\n", result);
    }
    
    // Cleanup
    free(request.input_format);
    free(request.output_format);
    free(request.output_path);
    document_destroy(doc);
    
    return result;
}