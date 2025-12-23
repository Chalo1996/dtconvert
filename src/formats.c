#include "../include/dtconvert.h"

// This file provides format-related utilities
// Currently used to ensure all functions are properly defined

// Simple format validation function
bool is_supported_format(const char *format) {
    if (!format) return false;
    
    const char *supported_formats[] = {
        "pdf", "docx", "txt", "csv", "odt", "xlsx", "json", "yaml", "sql", "tokens", "postgresql", "html", "md",
        NULL
    };
    
    for (int i = 0; supported_formats[i] != NULL; i++) {
        if (strcmp(format, supported_formats[i]) == 0) {
            return true;
        }
    }
    
    return false;
}

// Get format description
const char* get_format_description(const char *format) {
    if (!format) return "Unknown";
    
    struct {
        const char *format;
        const char *description;
    } format_descriptions[] = {
        {"pdf", "Portable Document Format"},
        {"docx", "Microsoft Word Document"},
        {"txt", "Plain Text File"},
        {"csv", "Comma Separated Values"},
        {"json", "JavaScript Object Notation"},
        {"yaml", "YAML Ain't Markup Language"},
        {"sql", "SQL (INSERT statements)"},
        {"odt", "OpenDocument Text"},
        {"xlsx", "Microsoft Excel Spreadsheet"},
        {"tokens", "Tokenized text (for ML/AI)"},
        {"postgresql", "PostgreSQL (import/store)"},
        {"html", "HyperText Markup Language"},
        {"md", "Markdown Document"},
        {NULL, "Unknown Format"}
    };
    
    for (int i = 0; format_descriptions[i].format != NULL; i++) {
        if (strcmp(format, format_descriptions[i].format) == 0) {
            return format_descriptions[i].description;
        }
    }
    
    return "Unknown Format";
}