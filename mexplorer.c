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

// A dynamic array that grows as needed - like ArrayList in Java
typedef struct {
    file_entry_t *arr;  // The actual array of files
    size_t used;        // How many items we have now
    size_t cap;         // How many items we can hold
} entry_list_t;

// All the state for the interactive UI
typedef struct {
    char *current_path;      // Where we are now (like /home/user)
    entry_list_t entries;    // Files in current folder
    int cursor_pos;          // Which file is highlighted
    int scroll_offset;       // For scrolling in large folders
    int needs_refresh;       // Redraw the screen?
    explorer_flags_t flags;  // Current settings
} interactive_state_t;

// Helper function declarations - these are "private" to this file
static void list_init(entry_list_t *l);
static void list_free(entry_list_t *l);
static void list_push(entry_list_t *l, file_entry_t *e);
static void human_size(off_t size, char *buf, size_t bufsz);
static void print_mode(mode_t mode, char *buf, size_t bufsz);
static void format_mtime(time_t epoch, char *buf, size_t bufsz);
static int include_entry(const file_entry_t *e, const explorer_flags_t *f);
static void read_dir(const char *dirpath, entry_list_t *out, const explorer_flags_t *flags);
static void print_entry(const file_entry_t *e, const explorer_flags_t *flags, int is_cursor);
static int get_terminal_height(void);
static void clear_screen(void);
static void setup_terminal(int enable_raw);
static char read_single_char(void);

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
        free(l->arr[i].name);   // Free the filename string
        free(l->arr[i].path);   // Free the full path string
    }
    free(l->arr);  // Free the array itself
    l->arr = NULL;
    l->used = 0;
    l->cap = 0;
}

// Add item to list, making it bigger if needed (like C++ vector.push_back())
static void list_push(entry_list_t *l, file_entry_t *e) {
    // Out of space? Double the capacity (or start with 64)
    if (l->used == l->cap) {
        size_t newcap = l->cap ? l->cap * 2 : 64;
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

// Clear screen using ANSI escape codes (works on most terminals)
static void clear_screen(void) {
    printf("\033[2J\033[H");  // \033[2J = clear, \033[H = move to top-left
}

// Put terminal in "raw mode" so we can read single keypresses
static void setup_terminal(int enable_raw) {
    static struct termios old_term, new_term;
    
    if (enable_raw) {
        tcgetattr(STDIN_FILENO, &old_term);  // Save current settings
        new_term = old_term;
        // Turn off line buffering and character echo
        new_term.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    } else {
        // Restore original terminal settings
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    }
}

// Read exactly one character (no waiting for Enter key)
static char read_single_char(void) {
    char c;
    read(STDIN_FILENO, &c, 1);
    return c;
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
        char mode[16], sizeb[32], timeb[32];
        print_mode(e->st.st_mode, mode, sizeof(mode));
        format_mtime(e->st.st_mtime, timeb, sizeof(timeb));

        // Convert user/group IDs to names
        struct passwd *pw = getpwuid(e->st.st_uid);
        struct group *gr = getgrgid(e->st.st_gid);
        
        // Format file size (pretty or raw bytes)
        if (flags->human_readable) {
            human_size(e->st.st_size, sizeb, sizeof(sizeb));
        } else {
            snprintf(sizeb, sizeof(sizeb), "%" PRIdMAX, (intmax_t)e->st.st_size);
        }
        
        // Print the main file info line (like ls -l)
        printf("%s %2ju %-8s %-8s %8s %s %s",
               mode,                       // File type and permissions
               (uintmax_t)e->st.st_nlink,  // Number of hard links
               pw ? pw->pw_name : "-",     // Owner name
               gr ? gr->gr_name : "-",     // Group name  
               sizeb,                      // File size
               timeb,                      // Modification time
               e->name);                   // Filename
               
        // If it's a symlink, show where it points
        if (S_ISLNK(e->st.st_mode)) {
            char buf[PATH_MAX];
            ssize_t r = readlink(e->path, buf, sizeof(buf) - 1);
            if (r > 0) {
                buf[r] = '\0';
                printf(" -> %s", buf);
            }
        }
    }
    
    // Turn off highlighting if we turned it on
    if (is_cursor) {
        printf("\033[0m");  // Reset text attributes
    }
    printf("\n");
}

// Read all files in a directory into our list
static void read_dir(const char *path, entry_list_t *out, const explorer_flags_t *f) {
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "opendir(%s): %s\n", path, strerror(errno));
        return;
    }

    struct dirent *ent;
    // Read each directory entry one by one
    while ((ent = readdir(d))) {
        // Skip the special . and .. entries
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        // Build full path by combining directory + filename
        size_t plen = strlen(path), nlen = strlen(ent->d_name);
        char *full = malloc(plen + nlen + 2);  // +2 for '/' and null terminator
        if (!full) {
            perror("malloc");
            continue;
        }
        sprintf(full, "%s/%s", path, ent->d_name);

        file_entry_t fe = {0};
        fe.name = strdup(ent->d_name);
        fe.path = full;
        fe.st_valid = (lstat(full, &fe.st) == 0);  // lstat works with symlinks

        // Only add to list if it passes our filters
        if (include_entry(&fe, f)) {
            list_push(out, &fe);
        } else {
            // Free memory if filtered out
            free(fe.name);
            free(fe.path);
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
    
    // Header with current location and settings
    printf("=== MEXPLORER: %s ===\n", state->current_path);
    printf("Settings: [Sort:%s] [Hidden:%s] [Format:%s] [Human:%s] [Filter:%s]\n\n",
           state->flags.sort_mode == SORT_NAME ? "Name" : 
           state->flags.sort_mode == SORT_SIZE ? "Size" : "Time",
           state->flags.show_all ? "ON" : "OFF",
           state->flags.long_format ? "Long" : "Short",
           state->flags.human_readable ? "ON" : "OFF",
           state->flags.dirs_only ? "Dirs" : 
           state->flags.files_only ? "Files" : "All");
    
    // Calculate how many files we can show based on terminal size
    int term_height = get_terminal_height();
    int available_lines = term_height - 6;  // Reserve space for header/footer
    
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
                printf("\033[7m%-40s\033[0m\n", state->entries.arr[i].name);
            } else {
                printf("%-40s\n", state->entries.arr[i].name);
            }
        }
    }
    
    // Footer with quick help
    printf("\nControls: j/k=Navigate, Enter=Open, b=Back, a=Hidden, l=Long, s=Sort, ?=Help, q=Quit\n");
}

// The main interactive UI loop - this is where the magic happens!
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
    
    list_init(&state.entries);
    setup_terminal(1);  // Enable raw mode for single-key input
    
    // Main event loop - runs until user quits
    int running = 1;
    while (running) {
        // Reload directory if needed (after navigation or setting changes)
        if (state.needs_refresh) {
            load_directory(&state);
            state.needs_refresh = 0;
        }
        
        // Draw the UI
        display_interface(&state);
        
        // Wait for and process user input
        char key = read_single_char();
        
        switch (key) {
            case 'q':  // Quit
                running = 0;
                break;
                
            case 'j':  // Move down
            case '\033':  // Arrow keys start with escape sequence
                if (key == '\033') {
                    // Arrow keys send 3 bytes: ESC [ A/B/C/D
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
                    file_entry_t *entry = &state.entries.arr[state.cursor_pos];  // FIXED: state. instead of state->
                    if (entry->st_valid && S_ISDIR(entry->st.st_mode)) {
                        // Navigate into directory
                        free(state.current_path);
                        state.current_path = strdup(entry->path);
                        state.needs_refresh = 1;
                    } else {
                        // For files, just show a message (could be extended to open files)
                        printf("\nFile: %s (press any key to continue)", entry->name);
                        read_single_char();
                    }
                }
                break;
                
            case 'b':  // Go back to parent directory
                {
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
                
            // REAL-TIME FLAG TOGGLES - These are the important new features!
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
                printf("\n=== MEXPLORER INTERACTIVE CONTROLS ===\n\n");
                printf("NAVIGATION:\n");
                printf("  j / k or ↓ / ↑  - Move cursor up/down\n");
                printf("  ENTER           - Open directory or file\n");
                printf("  b               - Go back to parent directory\n\n");
                
                printf("VIEW SETTINGS (toggle on/off):\n");
                printf("  a - Toggle hidden files (show/hide dotfiles)\n");
                printf("  l - Toggle long format (detailed/simple view)\n");
                printf("  H - Toggle human-readable file sizes\n");
                printf("  s - Cycle sort order (name → size → time)\n");
                printf("  d - Toggle directories only filter\n");
                printf("  f - Toggle files only filter\n");
                printf("  r - Refresh current directory view\n\n");
                
                printf("OTHER:\n");
                printf("  q - Quit the explorer\n");
                printf("  ? - Show this help screen\n\n");
                printf("Press any key to continue...");
                read_single_char();
                break;
        }
        
        // Auto-scroll to keep cursor in view
        int term_height = get_terminal_height();
        int available_lines = term_height - 6;
        
        if (state.cursor_pos < state.scroll_offset) {
            state.scroll_offset = state.cursor_pos;  // Scroll up if cursor above view
        } else if (state.cursor_pos >= state.scroll_offset + available_lines) {
            state.scroll_offset = state.cursor_pos - available_lines + 1;  // Scroll down
        }
    }
    
    // Cleanup before exit
    setup_terminal(0);  // Restore normal terminal behavior
    list_free(&state.entries);
    free(state.current_path);
    printf("\nThanks for using MEXPLORER!\n");
}

// Non-interactive mode: just list files and exit (like ls command)
void traverse_directory(const char *path, const explorer_flags_t *flags) {
    entry_list_t list;
    list_init(&list);
    read_dir(path, &list, flags);

    // Sort the files
    if (flags->sort_mode == SORT_NAME) {
        qsort(list.arr, list.used, sizeof(file_entry_t), cmp_name);
    } else if (flags->sort_mode == SORT_SIZE) {
        qsort(list.arr, list.used, sizeof(file_entry_t), cmp_size);
    } else if (flags->sort_mode == SORT_TIME) {
        qsort(list.arr, list.used, sizeof(file_entry_t), cmp_time);
    }

    // Print directory header and contents
    printf("%s:\n", path);
    for (size_t i = 0; i < list.used; i++) {
        print_entry(&list.arr[i], flags, 0);
    }
    printf("\n");

    // If recursive mode, go into subdirectories
    if (flags->recursive) {
        for (size_t i = 0; i < list.used; i++) {
            if (list.arr[i].st_valid && S_ISDIR(list.arr[i].st.st_mode)) {
                // Skip . and .. to avoid infinite loops
                if (strcmp(list.arr[i].name, ".") != 0 && 
                    strcmp(list.arr[i].name, "..") != 0) {
                    traverse_directory(list.arr[i].path, flags);
                }
            }
        }
    }
    list_free(&list);
}
