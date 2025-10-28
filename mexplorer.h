#ifndef MEXPLORER_H
#define MEXPLORER_H

#include <sys/stat.h>
#include <stddef.h>

// Sort modes for different ordering of files
typedef enum { 
    SORT_NAME,  // Alphabetical by filename
    SORT_SIZE,  // By file size (largest first)
    SORT_TIME   // By modification time (newest first)
} sort_mode_t;

// Structure representing one file entry with metadata
typedef struct {
    char *name;         // basename of the file
    char *path;         // full path to the file
    struct stat st;     // stat info (permissions, size, timestamps)
    int st_valid;       // 1 if stat() succeeded, 0 otherwise
    int is_selected;    // For interactive selection
} file_entry_t;

// Configuration flags passed from command line arguments
typedef struct {
    int show_all;           // -a: show hidden files (starting with .)
    int recursive;          // -r: traverse directories recursively (non-interactive mode)
    int long_format;        // -l: show detailed listing
    int dirs_only;          // -d: show only directories
    int files_only;         // -f: show only regular files
    int human_readable;     // -h: show sizes in human-readable format
    sort_mode_t sort_mode;  // Sorting method (name, size, time)
    int interactive;        // Whether to run in interactive mode
} explorer_flags_t;

// Function declarations
void traverse_directory(const char *path, const explorer_flags_t *flags);
void interactive_explorer(const char *start_path, const explorer_flags_t *flags);

#endif
