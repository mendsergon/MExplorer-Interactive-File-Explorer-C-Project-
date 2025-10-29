# MExplorer – Interactive File Explorer (C Project)

### Project Summary

**MExplorer** is a **terminal-based file explorer** written in C that provides both interactive and non-interactive modes for navigating the filesystem. It allows users to browse directories, view file details, and filter or sort files in real-time, mimicking features from common Unix utilities like `ls` and `mc` (Midnight Commander). The program leverages **POSIX APIs**, dynamic memory management, and terminal control to create a responsive and user-friendly console interface.

**Version:** 1.0.1  
**Latest Updates:** Performance optimizations for memory and system call efficiency

---

### Core Features

* **Interactive File Navigation** – Move through directories using keyboard keys (`j/k` or arrow keys) and open files or subdirectories.
* **Real-Time UI Controls** – Toggle hidden files, switch between short/long view, human-readable sizes, and sort order without restarting the program.
* **Sorting & Filtering** – Sort files by name, size, or modification time; filter to show only directories or files.
* **Recursive Directory Traversal** – Non-interactive mode supports recursive listing of files.
* **Detailed File Metadata** – View permissions, ownership, size, and modification time (similar to `ls -l`).
* **Symlink Resolution** – Displays symlink targets when present.
* **Terminal-Aware Display** – Adjusts the UI based on terminal height, supports scrolling in large directories.
* **Batch Mode** – Simple, script-friendly listing of directory contents without interactive UI.

---

### Key Methods and Algorithms

* **Dynamic Array Management:**
  Implements a resizable array (`entry_list_t`) to store file entries, similar to `std::vector` in C++, with power-of-two growth strategy for optimal performance.

* **File Metadata Handling:**
  Uses `lstat()` to gather file stats and stores them in `file_entry_t`, including size, permissions, and timestamps.

* **Sorting Mechanisms:**
  Provides `qsort()` comparators (`cmp_name`, `cmp_size`, `cmp_time`) to sort file entries dynamically.

* **Terminal Control:**
  Uses ANSI escape codes for clearing the screen and highlighting selected entries; employs `termios` for raw input mode to capture single keystrokes with cached terminal size detection.

* **Human-Readable Size Conversion:**
  Converts file sizes into readable units (B, K, M, G, T) for easier interpretation using thread-local buffers.

* **Recursive Directory Traversal:**
  Implements `traverse_directory()` for non-interactive listing, optionally visiting subdirectories when `-r` is used.

* **Interactive Event Loop:**
  Handles keypresses for navigation, toggles, and file operations while updating the display dynamically.

* **Memory Management:**
  Allocates and frees memory for file names and paths using combined allocations, ensuring no leaks during repeated directory loads.

---

### Performance Optimizations (v1.0.1)

* **Memory Efficiency:** Combined path and name storage in single allocation per file entry
* **System Call Reduction:** Cached terminal height detection with 500ms TTL
* **Input Handling:** Batched escape sequence reads for arrow key detection
* **Formatting Optimization:** Thread-local buffers for repeated string operations
* **Compiler Optimizations:** Aggressive flags for maximum performance

---

### Skills Demonstrated

* Terminal-based UI programming with ANSI escape codes
* File system exploration using POSIX APIs (`opendir`, `readdir`, `lstat`)
* Real-time interactive input handling in C
* Dynamic memory allocation and cleanup for complex data structures
* Sorting and filtering of structured data
* Recursive algorithm design for filesystem traversal
* Modular program design separating UI, file handling, and utility functions
* Command-line argument parsing with `getopt()`
* Performance optimization and system call reduction techniques
* Cross-platform compatible, text-based interface

---

### File Overview

| File Name       | Description                                                                      |
| --------------- | -------------------------------------------------------------------------------- |
| **main.c**      | Entry point; parses command-line arguments and selects interactive or batch mode |
| **mexplorer.h** | Header file; defines data structures, flags, and function prototypes             |
| **mexplorer.c** | Core implementation; interactive UI loop, file traversal, sorting, display logic |

---

### Technical Architecture

* **Interactive Mode:**
  Uses `interactive_explorer()` to render files, handle user input, and update display in real-time.

* **Non-Interactive Mode:**
  Uses `traverse_directory()` to list files recursively or non-recursively based on flags.

* **Data Structures:**
  `file_entry_t` holds file metadata, `entry_list_t` stores a dynamic list of entries, and `interactive_state_t` manages UI state.

* **Sorting & Filtering:**
  Sort modes: alphabetical, size, modification time; filtering by hidden, files-only, or directories-only.

* **Terminal Management:**
  `termios` for raw mode input, cached `ioctl` for terminal size, ANSI codes for screen clearing and highlighting.

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
  b               - Go back to parent directory

VIEW SETTINGS (toggle on/off):
  a - Toggle hidden files (show/hide dotfiles)
  l - Toggle long format (detailed/simple view)
  H - Toggle human-readable file sizes
  s - Cycle sort order (name → size → time)
  d - Toggle directories only filter
  f - Toggle files only filter
  r - Refresh current directory view

OTHER:
  q - Quit the explorer
  ? - Show help screen
```

---

### System Requirements

* **Operating System:** Linux/Unix
* **Compiler:** GCC supporting C11
* **Terminal:** ANSI-compatible console
* **Permissions:** Read access to directories and files to explore

---

### Version History

* **v1.0.1** - Performance optimizations: memory efficiency, system call reduction, input handling
* **v1.0.0** - Initial release with core file explorer functionality
