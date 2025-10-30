#define _XOPEN_SOURCE 700        // Need this for certain POSIX features
#define _POSIX_C_SOURCE 200809L

#include "mexplorer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>

// A dynamic array that grows as needed - like ArrayList in Java
typedef struct {
    file_entry_t *arr;  // The actual array of files
    size_t used;        // How many items we have now
    size_t cap;         // How many items we can hold
} entry_list_t;

// History stack for back navigation
typedef struct {
    char **paths;       // Array of path strings
    size_t size;        // Current number of paths in history
    size_t capacity;    // Maximum capacity of history
} history_stack_t;

// All the state for the interactive UI
typedef struct {
    char *current_path;      // Where we are now (like /home/user)
    entry_list_t entries;    // Files in current folder
    history_stack_t history; // Navigation history for back button
    int cursor_pos;          // Which file is highlighted
    int scroll_offset;       // For scrolling in large folders
    int needs_refresh;       // Redraw the screen?
    int terminal_resized;    // Terminal size changed?
    explorer_flags_t flags;  // Current settings
} interactive_state_t;

// Thread-local buffers for formatting to avoid repeated stack allocations
static __thread char human_buf[32];
static __thread char mode_buf[16];
static __thread char time_buf[32];

// Global state for signal handling
static interactive_state_t *global_state = NULL;

// Helper function declarations - these are "private" to this file
static void list_init(entry_list_t *l);
static void list_free(entry_list_t *l);
static void list_push(entry_list_t *l, file_entry_t *e);
static void history_init(history_stack_t *h);
static void history_free(history_stack_t *h);
static void history_push(history_stack_t *h, const char *path);
static char *history_pop(history_stack_t *h);
static int history_is_empty(const history_stack_t *h);
static void human_size(off_t size, char *buf, size_t bufsz);
static void print_mode(mode_t mode, char *buf, size_t bufsz);
static void format_mtime(time_t epoch, char *buf, size_t bufsz);
static int include_entry(const file_entry_t *e, const explorer_flags_t *f);
static void read_dir(const char *dirpath, entry_list_t *out, const explorer_flags_t *flags);
static void print_entry(const file_entry_t *e, const explorer_flags_t *flags, int is_cursor);
static int get_terminal_height(void);
static int get_terminal_width(void);
static int get_terminal_height_cached(void);
static void clear_screen(void);
static void setup_terminal(int enable_raw);
static char read_single_char_optimized(void);
static void restore_terminal_and_exit(interactive_state_t *state);
static void handle_terminal_resize(int sig);

// Compare functions for sorting - used by qsort()
static int cmp_name(const void *a, const void *b) {
    return strcmp(((file_entry_t*)a)->name, ((file_entry_t*)b)->name);
}

static int cmp_size(const void *a, const void *b) {
    const file_entry_t *x = a, *y = b;
    // Only compare sizes if we have valid file info for both
    if (x->st_valid && y->st_valid) {
        if (x->st.st_size == y->st.st_size) 
            return strcmp(x->name, y->name);  // Same size? Sort by name
        return (y->st.st_size > x->st.st_size) ? 1 : -1;  // Bigger files first
    }
    return cmp_name(a, b);  // Fallback to name sort if file info missing
}

static int cmp_time(const void *a, const void *b) {
    const file_entry_t *x = a, *y = b;
    if (x->st_valid && y->st_valid) {
        if (x->st.st_mtime == y->st.st_mtime) 
            return strcmp(x->name, y->name);
        return (y->st.st_mtime > x->st.st_mtime) ? 1 : -1;  // Newer files first
    }
    return cmp_name(a, b);
}

// Start with an empty list
static void list_init(entry_list_t *l) {
    l->arr = NULL;
    l->used = 0;
    l->cap = 0;
}

// Free all memory - important to avoid leaks!
static void list_free(entry_list_t *l) {
    for (size_t i = 0; i < l->used; i++) {
        // Free the combined path+name allocation
        free(l->arr[i].path);
        // Don't free name separately since it points into path
    }
    free(l->arr);  // Free the array itself
    l->arr = NULL;
    l->used = 0;
    l->cap = 0;
}

// Add item to list, making it bigger if needed (like C++ vector.push_back())
static void list_push(entry_list_t *l, file_entry_t *e) {
    // Out of space? Use power-of-two growth for better performance
    if (l->used == l->cap) {
        size_t newcap = l->cap ? l->cap * 2 : 64;
        // Round up to next power of two for some allocators
        if (newcap > 64) {
            newcap--;
            newcap |= newcap >> 1;
            newcap |= newcap >> 2;
            newcap |= newcap >> 4;
            newcap |= newcap >> 8;
            newcap |= newcap >> 16;
            newcap++;
        }
        
        file_entry_t *tmp = realloc(l->arr, newcap * sizeof(file_entry_t));
        if (!tmp) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        l->arr = tmp;
        l->cap = newcap;
    }
    l->arr[l->used++] = *e;  // Copy the entry and move to next slot
}

// Initialize history stack
static void history_init(history_stack_t *h) {
    h->paths = NULL;
    h->size = 0;
    h->capacity = 0;
}

// Free history stack memory
static void history_free(history_stack_t *h) {
    for (size_t i = 0; i < h->size; i++) {
        free(h->paths[i]);
    }
    free(h->paths);
    h->paths = NULL;
    h->size = 0;
    h->capacity = 0;
}

// Push a path to history
static void history_push(history_stack_t *h, const char *path) {
    // Resize if needed
    if (h->size == h->capacity) {
        size_t new_cap = h->capacity ? h->capacity * 2 : 16;
        char **tmp = realloc(h->paths, new_cap * sizeof(char*));
        if (!tmp) {
            perror("realloc");
            return;
        }
        h->paths = tmp;
        h->capacity = new_cap;
    }
    
    // Don't push if it's the same as the current top (to avoid duplicates)
    if (h->size > 0 && strcmp(h->paths[h->size - 1], path) == 0) {
        return;
    }
    
    h->paths[h->size++] = strdup(path);
}

// Pop a path from history
static char *history_pop(history_stack_t *h) {
    if (h->size == 0) {
        return NULL;
    }
    return h->paths[--h->size];
}

// Check if history is empty
static int history_is_empty(const history_stack_t *h) {
    return h->size == 0;
}

// Convert bytes to human readable like "1.5K" instead of "1536"
static void human_size(off_t size, char *buf, size_t bufsz) {
    const char *units[] = {"B", "K", "M", "G", "T"};  // Bytes, Kilobytes, etc.
    double s = (double)size;
    int i = 0;
    
    // Keep dividing by 1024 until we find the right unit
    while (s >= 1024.0 && i < 4) {
        s /= 1024.0;
        i++;
    }
    snprintf(buf, bufsz, "%.1f%s", s, units[i]);
}

// Convert file permissions to string like "drwxr-xr-x"
static void print_mode(mode_t m, char *buf, size_t n) {
    char type_char = '-';
    
    // First character shows file type
    if (S_ISDIR(m))       type_char = 'd';  // Directory
    else if (S_ISLNK(m))  type_char = 'l';  // Symbolic link
    else if (S_ISCHR(m))  type_char = 'c';  // Character device
    else if (S_ISBLK(m))  type_char = 'b';  // Block device
    else if (S_ISFIFO(m)) type_char = 'p';  // Pipe
    else if (S_ISSOCK(m)) type_char = 's';  // Socket
    
    // Build permission string: type + user/group/other permissions
    snprintf(buf, n, "%c%c%c%c%c%c%c%c%c%c", type_char,
            (m & S_IRUSR) ? 'r' : '-',  // User read
            (m & S_IWUSR) ? 'w' : '-',  // User write  
            (m & S_IXUSR) ? 'x' : '-',  // User execute
            (m & S_IRGRP) ? 'r' : '-',  // Group read
            (m & S_IWGRP) ? 'w' : '-',  // Group write
            (m & S_IXGRP) ? 'x' : '-',  // Group execute
            (m & S_IROTH) ? 'r' : '-',  // Others read
            (m & S_IWOTH) ? 'w' : '-',  // Others write
            (m & S_IXOTH) ? 'x' : '-'); // Others execute
}

// Convert Unix timestamp to readable date/time
static void format_mtime(time_t t, char *b, size_t n) {
    struct tm tm;
    localtime_r(&t, &tm);  // Convert to local time (thread-safe version)
    strftime(b, n, "%Y-%m-%d %H:%M", &tm);  // Format like "2024-01-15 14:30"
}

// Should we show this file based on current filters?
static int include_entry(const file_entry_t *e, const explorer_flags_t *f) {
    // Skip hidden files (starting with .) unless show_all is on
    if (!f->show_all && e->name[0] == '.') 
        return 0;
    
    // Apply type filters if set
    if (f->dirs_only && !(e->st_valid && S_ISDIR(e->st.st_mode))) 
        return 0;  // Not a directory but we only want directories
    if (f->files_only && !(e->st_valid && S_ISREG(e->st.st_mode))) 
        return 0;  // Not a regular file but we only want files
    
    return 1;  // Passed all filters
}

// Get terminal height so we know how many lines we can display
static int get_terminal_height(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_row;  // Return number of rows terminal has
    }
    return 24; // Safe default if we can't detect
}

// Get terminal width
static int get_terminal_width(void) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return w.ws_col;
    }
    return 80; // Safe default
}

// Cached version to reduce frequent ioctl calls
static int get_terminal_height_cached(void) {
    static int cached_height = 0;
    static time_t last_check = 0;
    time_t now = time(NULL);
    
    // Only check every 500ms to reduce system calls
    if (cached_height == 0 || now - last_check > 0.5) {
        cached_height = get_terminal_height();
        last_check = now;
    }
    return cached_height;
}

// Clear screen using ANSI escape codes (works on most terminals)
static void clear_screen(void) {
    printf("\033[2J\033[H");  // \033[2J = clear, \033[H = move to top-left
    fflush(stdout);
}

// Put terminal in "raw mode" so we can read single keypresses
static void setup_terminal(int enable_raw) {
    static struct termios old_term, new_term;
    static int terminal_setup = 0;
    
    if (enable_raw) {
        tcgetattr(STDIN_FILENO, &old_term);  // Save current settings
        new_term = old_term;
        // Turn off line buffering and character echo
        new_term.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        terminal_setup = 1;
    } else if (terminal_setup) {
        // Restore original terminal settings only if we set them up
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        terminal_setup = 0;
    }
}

// Signal handler for terminal resize
static void handle_terminal_resize(int sig) {
    (void)sig;  // Mark parameter as unused to avoid warning
    if (global_state) {
        global_state->terminal_resized = 1;
        global_state->needs_refresh = 1;
    }
}

// Read exactly one character (no waiting for Enter key) - optimized version
static char read_single_char_optimized(void) {
    char buf[8];
    ssize_t bytes_read = read(STDIN_FILENO, buf, sizeof(buf));
    
    if (bytes_read <= 0) return 0;
    
    // Handle escape sequences in one read
    if (buf[0] == '\033' && bytes_read >= 3 && buf[1] == '[') {
        switch(buf[2]) {
            case 'A': return 'k';  // Up arrow
            case 'B': return 'j';  // Down arrow
            case 'C': return 'l';  // Right arrow  
            case 'D': return 'h';  // Left arrow
        }
    }
    
    return buf[0];  // Return first character
}

// Print one file entry, with optional highlighting for selected item
static void print_entry(const file_entry_t *e, const explorer_flags_t *flags, int is_cursor) {
    // Highlight selected item with reverse video
    if (is_cursor) {
        printf("\033[7m");  // Start reverse video
    }
    
    // Show error placeholders if we couldn't read file info
    if (!e->st_valid) {
        printf("??????????\t? ? ? ?????????? ?????????????????? %s", e->name);
    } else {
        // Use thread-local buffers to avoid repeated stack allocations
        print_mode(e->st.st_mode, mode_buf, sizeof(mode_buf));
        format_mtime(e->st.st_mtime, time_buf, sizeof(time_buf));

        // Convert user/group IDs to names
        struct passwd *pw = getpwuid(e->st.st_uid);
        struct group *gr = getgrgid(e->st.st_gid);
        
        // Format file size (pretty or raw bytes)
        if (flags->human_readable) {
            human_size(e->st.st_size, human_buf, sizeof(human_buf));
            printf("%s %2ju %-8s %-8s %8s %s %s",
                   mode_buf,                       // File type and permissions
                   (uintmax_t)e->st.st_nlink,      // Number of hard links
                   pw ? pw->pw_name : "-",         // Owner name
                   gr ? gr->gr_name : "-",         // Group name  
                   human_buf,                      // File size
                   time_buf,                       // Modification time
                   e->name);                       // Filename
        } else {
            printf("%s %2ju %-8s %-8s %8" PRIdMAX " %s %s",
                   mode_buf,                       // File type and permissions
                   (uintmax_t)e->st.st_nlink,      // Number of hard links
                   pw ? pw->pw_name : "-",         // Owner name
                   gr ? gr->gr_name : "-",         // Group name  
                   (intmax_t)e->st.st_size,        // File size in bytes
                   time_buf,                       // Modification time
                   e->name);                       // Filename
        }
               
        // If it's a symlink, show where it points
        if (S_ISLNK(e->st.st_mode)) {
            char link_buf[PATH_MAX];
            ssize_t r = readlink(e->path, link_buf, sizeof(link_buf) - 1);
            if (r > 0) {
                link_buf[r] = '\0';
                printf(" -> %s", link_buf);
            }
        }
    }
    
    // Turn off highlighting if we turned it on
    if (is_cursor) {
        printf("\033[0m");  // Reset text attributes
    }
    printf("\n");
}

// Read all files in a directory into our list - optimized version
static void read_dir(const char *path, entry_list_t *out, const explorer_flags_t *f) {
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "opendir(%s): %s\n", path, strerror(errno));
        return;
    }

    struct dirent *ent;
    size_t path_len = strlen(path);
    
    // Read each directory entry one by one
    while ((ent = readdir(d))) {
        // Skip the special . and .. entries
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        // Calculate required sizes once
        size_t name_len = strlen(ent->d_name);
        size_t total_len = path_len + name_len + 2;  // +2 for '/' and null terminator
        
        // Single allocation for combined path + name
        char *full_path = malloc(total_len);
        if (!full_path) {
            perror("malloc");
            continue;
        }
        
        // Build path efficiently in one operation
        snprintf(full_path, total_len, "%s/%s", path, ent->d_name);

        file_entry_t fe = {0};
        fe.path = full_path;
        fe.name = full_path + path_len + 1;  // Name points into the path string
        fe.st_valid = (lstat(full_path, &fe.st) == 0);  // lstat works with symlinks

        // Only add to list if it passes our filters
        if (include_entry(&fe, f)) {
            list_push(out, &fe);
        } else {
            // Free the single allocation if filtered out
            free(full_path);
        }
    }
    closedir(d);
}

// Load or reload current directory contents
static void load_directory(interactive_state_t *state) {
    list_free(&state->entries);  // Clear old entries
    list_init(&state->entries);
    read_dir(state->current_path, &state->entries, &state->flags);
    
    // Sort based on current sort mode
    if (state->flags.sort_mode == SORT_NAME) {
        qsort(state->entries.arr, state->entries.used, sizeof(file_entry_t), cmp_name);
    } else if (state->flags.sort_mode == SORT_SIZE) {
        qsort(state->entries.arr, state->entries.used, sizeof(file_entry_t), cmp_size);
    } else if (state->flags.sort_mode == SORT_TIME) {
        qsort(state->entries.arr, state->entries.used, sizeof(file_entry_t), cmp_time);
    }
    
    // Reset UI state
    state->cursor_pos = 0;
    state->scroll_offset = 0;
}

// Draw the entire interactive UI
static void display_interface(interactive_state_t *state) {
    clear_screen();
    
    // Get terminal dimensions
    int term_height = get_terminal_height_cached();
    int term_width = get_terminal_width();
    
    // Header with current location and settings
    printf("\033[1;36m=== MEXPLORER: %s ===\033[0m\n", state->current_path);
    
    // Truncate path if too long for terminal
    char path_display[term_width];
    snprintf(path_display, sizeof(path_display), "%s", state->current_path);
    size_t path_len = strlen(path_display);
    if (path_len > (size_t)(term_width - 20)) {
        path_display[term_width - 23] = '\0';
        strcat(path_display, "...");
    }
    
    printf("Settings: [Sort:%s] [Hidden:%s] [Format:%s] [Human:%s] [Filter:%s]\n\n",
           state->flags.sort_mode == SORT_NAME ? "Name" : 
           state->flags.sort_mode == SORT_SIZE ? "Size" : "Time",
           state->flags.show_all ? "ON" : "OFF",
           state->flags.long_format ? "Long" : "Short",
           state->flags.human_readable ? "ON" : "OFF",
           state->flags.dirs_only ? "Dirs" : 
           state->flags.files_only ? "Files" : "All");
    
    // Calculate how many files we can show based on terminal size
    int available_lines = term_height - 6;  // Reserve space for header/footer
    
    if (available_lines < 1) {
        available_lines = 1;  // Minimum display area
    }
    
    // Adjust scroll position to keep cursor in view
    if (state->cursor_pos < state->scroll_offset) {
        state->scroll_offset = state->cursor_pos;
    } else if (state->cursor_pos >= state->scroll_offset + available_lines) {
        state->scroll_offset = state->cursor_pos - available_lines + 1;
    }
    
    // Figure out which slice of files to display (for scrolling)
    size_t start = state->scroll_offset;
    size_t end = start + available_lines;
    if (end > state->entries.used) {
        end = state->entries.used;
    }
    
    // Show the visible files
    for (size_t i = start; i < end; i++) {
        int is_cursor = (i == (size_t)state->cursor_pos);
        if (state->flags.long_format) {
            print_entry(&state->entries.arr[i], &state->flags, is_cursor);
        } else {
            // Simple view - just filenames with highlighting
            if (is_cursor) {
                printf("\033[7m%s\033[0m\n", state->entries.arr[i].name);
            } else {
                printf("%s\n", state->entries.arr[i].name);
            }
        }
    }
    
    // Fill remaining space if we have fewer files than available lines
    int lines_used = end - start;
    for (int i = lines_used; i < available_lines; i++) {
        printf("~\n");
    }
    
    // Footer with quick help
    printf("\n\033[1;33mControls:\033[0m j/k=Navigate, Enter=Open, b=Back, a=Hidden, l=Long, s=Sort, H=Human, d=Dirs, f=Files, r=Refresh, ?=Help, q=Quit\n");
    
    fflush(stdout);
}

// Non-interactive directory traversal
void traverse_directory(const char *path, const explorer_flags_t *flags) {
    entry_list_t entries;
    list_init(&entries);
    read_dir(path, &entries, flags);
    
    // Sort entries
    if (flags->sort_mode == SORT_NAME) {
        qsort(entries.arr, entries.used, sizeof(file_entry_t), cmp_name);
    } else if (flags->sort_mode == SORT_SIZE) {
        qsort(entries.arr, entries.used, sizeof(file_entry_t), cmp_size);
    } else if (flags->sort_mode == SORT_TIME) {
        qsort(entries.arr, entries.used, sizeof(file_entry_t), cmp_time);
    }
    
    // Print all entries
    for (size_t i = 0; i < entries.used; i++) {
        if (flags->long_format) {
            print_entry(&entries.arr[i], flags, 0);
        } else {
            printf("%s\n", entries.arr[i].name);
        }
    }
    
    list_free(&entries);
}

// Proper cleanup function
static void restore_terminal_and_exit(interactive_state_t *state) {
    // Switch back to main screen buffer
    printf("\033[?1049l");  // Switch back to main screen
    
    // Remove signal handlers
    signal(SIGWINCH, SIG_DFL);
    
    setup_terminal(0);  // Restore normal terminal mode
    clear_screen();     // Clear the screen
    
    // Reset all terminal attributes
    printf("\033[0m");  // Reset colors and attributes
    fflush(stdout);
    
    list_free(&state->entries);
    history_free(&state->history);
    free(state->current_path);
    
    // Clear the global state pointer
    global_state = NULL;
    
    // Show thank you message
    printf("Thank you for using MExplorer!\n");
    printf("File explorer session ended.\n\n");
}

// The main interactive UI loop
void interactive_explorer(const char *start_path, const explorer_flags_t *flags) {
    interactive_state_t state = {0};
    // Get absolute path (resolves symlinks, removes .., etc.)
    state.current_path = realpath(start_path, NULL);
    if (!state.current_path) {
        perror("realpath");
        return;
    }
    state.flags = *flags;  // Copy initial settings
    state.needs_refresh = 1;
    state.terminal_resized = 0;
    
    // Set global state for signal handling
    global_state = &state;
    
    list_init(&state.entries);
    history_init(&state.history);
    
    // Setup signal handler for terminal resize
    signal(SIGWINCH, handle_terminal_resize);
    
    setup_terminal(1);  // Enable raw mode for single-key input
    
    // Switch to alternate screen buffer (like btop, vim, htop)
    printf("\033[?1049h");  // Switch to alternate screen
    fflush(stdout);
    
    // Load initial directory immediately
    load_directory(&state);
    
    // Main event loop - runs until user quits
    int running = 1;
    while (running) {
        // Handle terminal resize
        if (state.terminal_resized) {
            state.terminal_resized = 0;
            state.needs_refresh = 1;
            // Force refresh of terminal size cache
            get_terminal_height_cached();
        }
        
        // Reload directory if needed (after navigation or setting changes)
        if (state.needs_refresh) {
            load_directory(&state);
            state.needs_refresh = 0;
        }
        
        // Draw the UI
        display_interface(&state);
        
        // Wait for and process user input
        char key = read_single_char_optimized();
        
        switch (key) {
            case 'q':  // Quit
                running = 0;
                break;
                
            case 'j':  // Move down
            case '\033':  // Arrow keys start with escape sequence
                if (key == '\033') {
                    // Arrow keys are now handled in read_single_char_optimized
                    // This case is kept for compatibility
                    char seq[2];
                    if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                        if (seq[0] == '[') {
                            if (seq[1] == 'B') key = 'j';  // Down arrow
                            else if (seq[1] == 'A') key = 'k';  // Up arrow
                        }
                    }
                }
                /* fall through */  // Intentional fallthrough to movement handling
                
            case 'k':  // Move up
                if (key == 'j' || key == '\033') {
                    if (state.cursor_pos < (int)state.entries.used - 1) {
                        state.cursor_pos++;
                    }
                } else if (key == 'k') {
                    if (state.cursor_pos > 0) {
                        state.cursor_pos--;
                    }
                }
                break;
                
            case '\n':  // Enter key - open file or directory
                if (state.entries.used > 0) {
                    file_entry_t *entry = &state.entries.arr[state.cursor_pos];
                    if (entry->st_valid && S_ISDIR(entry->st.st_mode)) {
                        // Save current directory to history before navigating
                        history_push(&state.history, state.current_path);
                        
                        // Navigate into directory
                        free(state.current_path);
                        state.current_path = strdup(entry->path);
                        state.needs_refresh = 1;
                    } else {
                        // For files, just show a message (could be extended to open files)
                        clear_screen();
                        printf("File: %s\n", entry->name);
                        printf("Path: %s\n", entry->path);
                        if (entry->st_valid) {
                            printf("Size: %ld bytes\n", (long)entry->st.st_size);
                            format_mtime(entry->st.st_mtime, time_buf, sizeof(time_buf));
                            printf("Modified: %s\n", time_buf);
                            print_mode(entry->st.st_mode, mode_buf, sizeof(mode_buf));
                            printf("Permissions: %s\n", mode_buf);
                        }
                        printf("\nPress any key to continue...");
                        fflush(stdout);
                        read_single_char_optimized();
                        state.needs_refresh = 1;
                    }
                }
                break;
                
            case 'b':  // Go back to previous directory using history
                if (!history_is_empty(&state.history)) {
                    char *prev_path = history_pop(&state.history);
                    if (prev_path && strcmp(prev_path, state.current_path) != 0) {
                        free(state.current_path);
                        state.current_path = strdup(prev_path);
                        state.needs_refresh = 1;
                    }
                    free(prev_path);  // Free the popped path
                } else {
                    // If no history, try to go to parent directory as fallback
                    char *parent = realpath("..", NULL);
                    if (parent && strcmp(parent, state.current_path) != 0) {
                        free(state.current_path);
                        state.current_path = parent;
                        state.needs_refresh = 1;
                    } else {
                        free(parent);
                    }
                }
                break;
                
            // REAL-TIME FLAG TOGGLES - These are the important features!
            case 'a':  // Toggle hidden files
                state.flags.show_all = !state.flags.show_all;
                state.needs_refresh = 1;
                break;
                
            case 'l':  // Toggle long/short view
                state.flags.long_format = !state.flags.long_format;
                break;
                
            case 's':  // Cycle through sort modes
                state.flags.sort_mode = (state.flags.sort_mode + 1) % 3;
                state.needs_refresh = 1;
                break;
                
            case 'H':  // Toggle human-readable sizes (shift+h)
                state.flags.human_readable = !state.flags.human_readable;
                break;
                
            case 'd':  // Show only directories
                state.flags.dirs_only = !state.flags.dirs_only;
                if (state.flags.dirs_only) state.flags.files_only = 0;
                state.needs_refresh = 1;
                break;
                
            case 'f':  // Show only files
                state.flags.files_only = !state.flags.files_only;
                if (state.flags.files_only) state.flags.dirs_only = 0;
                state.needs_refresh = 1;
                break;
                
            case 'r':  // Refresh (re-read directory)
                state.needs_refresh = 1;
                break;
                
            case '?':  // Show help
                clear_screen();
                printf("\033[1;35mMEXPLORER - INTERACTIVE FILE EXPLORER\033[0m\n\n");
                printf("\033[1;33mNAVIGATION:\033[0m\n");
                printf("  j / k or ↓ / ↑  - Move cursor up/down\n");
                printf("  ENTER           - Open directory or file\n");
                printf("  b               - Go back to previous directory\n\n");
                printf("\033[1;33mVIEW SETTINGS (toggle on/off):\033[0m\n");
                printf("  a - Toggle hidden files (show/hide dotfiles)\n");
                printf("  l - Toggle long format (detailed/simple view)\n");
                printf("  H - Toggle human-readable file sizes\n");
                printf("  s - Cycle sort order (name → size → time)\n");
                printf("  d - Toggle directories only filter\n");
                printf("  f - Toggle files only filter\n");
                printf("  r - Refresh current directory view\n\n");
                printf("\033[1;33mOTHER:\033[0m\n");
                printf("  q - Quit the explorer\n");
                printf("  ? - Show this help screen\n\n");
                printf("Press any key to continue...");
                fflush(stdout);
                read_single_char_optimized();
                state.needs_refresh = 1;
                break;
                
            default:
                // Ignore unknown keys
                break;
        }
    }
    
    // PROPER CLEANUP
    restore_terminal_and_exit(&state);
}
