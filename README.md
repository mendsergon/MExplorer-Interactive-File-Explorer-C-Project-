# MExplorer – Interactive File Explorer (C Project)

### Project Summary

**MExplorer** is a **terminal-based file explorer** written in C that provides both interactive and non-interactive modes for navigating the filesystem. It allows users to browse directories, view file details, and perform file operations in real-time, mimicking features from common Unix utilities like `ls` and `mc` (Midnight Commander). The program leverages **POSIX APIs**, dynamic memory management, and terminal control to create a responsive and user-friendly console interface.

**Version:** 1.0.3  
**Latest Updates:** Complete file operations suite (create, delete, copy, move, paste), clipboard system, position indicator

---

### Core Features

* **Interactive File Navigation** – Move through directories using keyboard keys (`j/k` or arrow keys) and open files or subdirectories.
* **File Operations Suite** – Create files/directories (`touch`/`mkdir`), delete with confirmation, copy/move/paste with clipboard system.
* **Navigation History** – Proper back navigation through visited directories using stack-based history.
* **Alternate Screen Buffer** – Uses terminal alternate screen to prevent scrollback artifacts.
* **Real-Time UI Controls** – Toggle hidden files, switch between short/long view, human-readable sizes, and sort order without restarting the program.
* **Sorting & Filtering** – Sort files by name, size, or modification time; filter to show only directories or files.
* **Recursive Directory Operations** – Supports recursive copying of directories and their contents.
* **Detailed File Metadata** – View permissions, ownership, size, and modification time (similar to `ls -l`).
* **Symlink Resolution** – Displays symlink targets when present.
* **Terminal-Aware Display** – Adjusts the UI based on terminal height, supports scrolling in large directories, handles terminal resizes.
* **Batch Mode** – Simple, script-friendly listing of directory contents without interactive UI.
* **Clipboard System** – Copy/cut files and directories between locations with visual feedback.

---

### Key Methods and Algorithms

* **Dynamic Array Management:**
  Implements a resizable array (`entry_list_t`) to store file entries with power-of-two growth strategy for optimal performance.

* **Navigation History Stack:**
  Implements stack-based history (`history_stack_t`) for proper back navigation through visited directories.

* **File Operations System:**
  Implements clipboard-based copy/move operations with recursive directory support using `copy_file()` and `copy_directory()` functions.

* **File Metadata Handling:**
  Uses `lstat()` to gather file stats and stores them in `file_entry_t`, including size, permissions, and timestamps.

* **Sorting Mechanisms:**
  Provides `qsort()` comparators (`cmp_name`, `cmp_size`, `cmp_time`) to sort file entries dynamically.

* **Terminal Control:**
  Uses ANSI escape codes for clearing the screen and highlighting selected entries; employs `termios` for raw input mode to capture single keystrokes with cached terminal size detection; uses alternate screen buffer to prevent scrollback artifacts.

* **Human-Readable Size Conversion:**
  Converts file sizes into readable units (B, K, M, G, T) for easier interpretation using thread-local buffers.

* **Recursive Directory Operations:**
  Implements recursive copying and traversal algorithms for comprehensive file management.

* **Interactive Event Loop:**
  Handles keypresses for navigation, toggles, and file operations while updating the display dynamically.

* **Memory Management:**
  Allocates and frees memory for file names and paths using combined allocations, ensuring no leaks during repeated directory loads.

---

### Performance Optimizations (v1.0.3)

* **Memory Efficiency:** Combined path and name storage in single allocation per file entry
* **System Call Reduction:** Cached terminal height detection with 500ms TTL
* **Input Handling:** Batched escape sequence reads for arrow key detection
* **Formatting Optimization:** Thread-local buffers for repeated string operations
* **Compiler Optimizations:** Aggressive flags for maximum performance
* **Terminal Optimization:** Alternate screen buffer for clean display
* **Efficient File Operations:** Stream-based file copying with 8KB buffers

---

### Skills Demonstrated

* Terminal-based UI programming with ANSI escape codes
* File system exploration using POSIX APIs (`opendir`, `readdir`, `lstat`)
* Real-time interactive input handling in C
* Dynamic memory allocation and cleanup for complex data structures
* Stack-based navigation history implementation
* File operations (create, delete, copy, move) with error handling
* Sorting and filtering of structured data
* Recursive algorithm design for filesystem operations
* Clipboard system implementation for file management
* Modular program design separating UI, file handling, and utility functions
* Command-line argument parsing with `getopt()`
* Performance optimization and system call reduction techniques
* Terminal signal handling (SIGWINCH) for resize events
* Cross-platform compatible, text-based interface

---

### File Overview

| File Name       | Description                                                                      |
| --------------- | -------------------------------------------------------------------------------- |
| **main.c**      | Entry point; parses command-line arguments and selects interactive or batch mode |
| **mexplorer.h** | Header file; defines data structures, flags, and function prototypes             |
| **mexplorer.c** | Core implementation; interactive UI loop, file operations, sorting, display logic |

---

### Technical Architecture

* **Interactive Mode:**
  Uses `interactive_explorer()` to render files, handle user input, and update display in real-time.

* **Non-Interactive Mode:**
  Uses `traverse_directory()` to list files recursively or non-recursively based on flags.

* **File Operations:**
  Clipboard-based system for copy/move operations with recursive directory support.

* **Data Structures:**
  `file_entry_t` holds file metadata, `entry_list_t` stores a dynamic list of entries, `history_stack_t` manages navigation history, and `interactive_state_t` manages UI state and clipboard.

* **Sorting & Filtering:**
  Sort modes: alphabetical, size, modification time; filtering by hidden, files-only, or directories-only.

* **Terminal Management:**
  `termios` for raw mode input, cached `ioctl` for terminal size, ANSI codes for screen clearing and highlighting, alternate screen buffer for clean display.

* **Memory Management:**
  Combined allocations for path/name storage, power-of-two array growth, thread-local formatting buffers.

---

### How to Compile and Run

1. Compile using GCC on Linux with performance optimizations:

   ```bash
   gcc -Wall -Wextra -std=c11 -O3 -march=native -flto -DNDEBUG -o mexplorer main.c mexplorer.c
   ```

2. Run interactively (default):

   ```bash
   ./mexplorer [options] [directory]
   ```

3. Run in batch mode (simple listing):

   ```bash
   ./mexplorer -b [directory]
   ```

4. Use command-line options for initial settings:

   ```
   -a : show hidden files
   -l : long format
   -h : human-readable sizes
   -S : sort by size
   -t : sort by time
   -n : sort by name (default)
   -d : directories only
   -f : files only
   -i : interactive mode (default)
   -b : batch mode
   ```

---

### Interactive Controls

Once running in interactive mode:

```
NAVIGATION:
  j / k or ↓ / ↑  - Move cursor up/down
  ENTER           - Open directory or file
  b               - Go back to previous directory (navigation history)

VIEW SETTINGS (toggle on/off):
  a - Toggle hidden files (show/hide dotfiles)
  l - Toggle long format (detailed/simple view)
  H - Toggle human-readable file sizes
  s - Cycle sort order (name → size → time)
  d - Toggle directories only filter
  f - Toggle files only filter
  r - Refresh current directory view

FILE OPERATIONS:
  n - Create new file or directory (inline prompt)
  D - Delete selected file/directory (with confirmation)
  c - Copy selected file/directory to clipboard
  m - Move (cut) selected file/directory to clipboard
  p - Paste from clipboard to current directory

OTHER:
  q - Quit the explorer
  ? - Show help screen
```

---

### System Requirements

* **Operating System:** Linux/Unix
* **Compiler:** GCC supporting C11
* **Terminal:** ANSI-compatible console
* **Permissions:** Read/write access to directories and files for operations

---

### Version History

* **v1.0.3** - Complete file operations (create, delete, copy, move, paste), clipboard system, position indicator
* **v1.0.2** - Navigation history, alternate screen buffer, immediate startup
* **v1.0.1** - Performance optimizations: memory efficiency, system call reduction, input handling
* **v1.0.0** - Initial release with core file explorer functionality
