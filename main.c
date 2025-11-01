#define _POSIX_C_SOURCE 200809L  // Tell compiler we want modern POSIX features
#include "mexplorer.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Show how to use the program when user messes up or asks for help
static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options] [directory]\n"
            "Interactive mode controls (once running):\n"
            "  j/k or ↓/↑ - Move selection up/down\n"
            "  enter      - Open file/folder\n" 
            "  b          - Go back to parent folder\n"
            "  a          - Toggle hidden files (show/hide dotfiles)\n"
            "  l          - Toggle detailed view\n"
            "  s          - Change sort order (name→size→time)\n"
            "  H          - Toggle human-readable sizes\n"
            "  d          - Show only directories\n" 
            "  f          - Show only files\n"
            "  n          - Create new file/directory\n"
            "  D          - Delete selected file/directory\n"
            "  r          - Refresh view\n"
            "  q          - Quit\n"
            "  ?          - Show this help\n\n"
            
            "Startup options (for command line):\n"
            "  -a Start with hidden files shown\n"
            "  -l Start in detailed view\n"
            "  -h Start with human-readable sizes\n"
            "  -S Start sorted by size\n"
            "  -t Start sorted by time\n"
            "  -n Start sorted by name (default)\n"
            "  -d Start with directories only\n"
            "  -f Start with files only\n"
            "  -i Interactive mode (default)\n"
            "  -b Batch mode (simple list and exit)\n",
            prog);
}

// Main function
int main(int argc, char **argv) {
    // Start with all flags turned off (0 means false/no)
    explorer_flags_t flags = {0};
    flags.sort_mode = SORT_NAME;  // Default to alphabetical order
    flags.interactive = 1;        // Start in fancy UI mode by default

    // Parse command line arguments like -a -l -S
    // getopt() is a standard Unix function for this
    int opt;
    while ((opt = getopt(argc, argv, "arlStndfhib")) != -1) {
        switch (opt) {
            case 'a': flags.show_all = 1; break;           // Show hidden files
            case 'r': flags.recursive = 1; break;          // Go into subfolders
            case 'l': flags.long_format = 1; break;        // Detailed view
            case 'S': flags.sort_mode = SORT_SIZE; break;  // Sort by size
            case 't': flags.sort_mode = SORT_TIME; break;  // Sort by time
            case 'n': flags.sort_mode = SORT_NAME; break;  // Sort by name
            case 'd': flags.dirs_only = 1; break;          // Only folders
            case 'f': flags.files_only = 1; break;         // Only files
            case 'h': flags.human_readable = 1; break;     // Pretty sizes
            case 'i': flags.interactive = 1; break;        // UI mode
            case 'b': flags.interactive = 0; break;        // Simple list mode
            default:
                usage(argv[0]);  // Show help if unknown option
                return EXIT_FAILURE;
        }
    }
    
    // Can't show only folders AND only files 
    if (flags.dirs_only && flags.files_only) {
        fprintf(stderr, "Error: Can't use -d (dirs only) and -f (files only) together.\n");
        return EXIT_FAILURE;
    }

    // Figure out where to start looking
    const char *start_dir = ".";  // Default: current folder
    if (optind < argc) {
        start_dir = argv[optind]; // Use folder user specified
    }

    // Choose between fancy UI mode or simple list mode
    if (flags.interactive) {
        interactive_explorer(start_dir, &flags);
    } else {
        // Simple mode: just list everything and exit
        traverse_directory(start_dir, &flags);
    }

    return EXIT_SUCCESS;  // Everything worked!
}
