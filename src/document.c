#include "../include/dtconvert.h"
#include <sys/stat.h>

Document* document_create(const char *path) {
    if (!path) return NULL;
    
    Document *doc = malloc(sizeof(Document));
    if (!doc) return NULL;
    
    memset(doc, 0, sizeof(Document));
    
    // Store path
    doc->path = strdup(path);
    if (!doc->path) {
        free(doc);
        return NULL;
    }
    
    // Get absolute path (avoid fixed buffers; required for _FORTIFY_SOURCE)
    char *resolved = realpath(path, NULL);
    if (resolved) {
        doc->full_path = resolved; // already malloc'ed
    } else {
        doc->full_path = strdup(path);
    }
    if (!doc->full_path) {
        free(doc->path);
        free(doc);
        return NULL;
    }
    
    // Extract filename
    char *filename = strrchr(doc->full_path, '/');
    if (filename) {
        filename++; // Skip the slash
    } else {
        filename = doc->full_path;
    }
    doc->filename = strdup(filename);
    if (!doc->filename) {
        document_destroy(doc);
        return NULL;
    }
    
    // Extract extension
    char *dot = strrchr(filename, '.');
    if (dot && dot != filename) {
        doc->extension = strdup(dot + 1);
        str_lower(doc->extension);
    } else {
        doc->extension = strdup("");
    }
    if (!doc->extension) {
        document_destroy(doc);
        return NULL;
    }
    
    // Check file existence and get size
    struct stat st;
    if (stat(doc->full_path, &st) == 0) {
        doc->exists = true;
        doc->size = st.st_size;
    } else {
        doc->exists = false;
        doc->size = 0;
    }
    
    return doc;
}

void document_destroy(Document *doc) {
    if (!doc) return;
    
    if (doc->path) free(doc->path);
    if (doc->filename) free(doc->filename);
    if (doc->extension) free(doc->extension);
    if (doc->full_path) free(doc->full_path);
    
    free(doc);
}

bool document_exists(const Document *doc) {
    return doc && doc->exists;
}

char* document_get_extension(const char *filename) {
    if (!filename) return NULL;
    
    char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return NULL;
    
    char *ext = strdup(dot + 1);
    if (ext) str_lower(ext);
    
    return ext;
}