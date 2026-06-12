# PA5: Contiguous Memory Allocation

For this project, I implemented a complete contiguous memory allocation simulator in C. The goal was to model how an operating system manages a single block of memory using partitioning, allocation strategies, releasing, compaction, and fragmentation reporting.

## Features
- **Interactive Command-Based Simulator**  
  The program runs in a custom shell (`allocator>`) that accepts memory commands:
  - `RQ <pid> <size> <F|B|W>` — request memory using First-Fit, Best-Fit, or Worst-Fit
  - `RL <pid>` — release memory for a process
  - `STAT` — show memory state and fragmentation
  - `STAT V` — show a visual memory map
  - `C` — compact memory to eliminate holes
  - `SIM <file>` — run a scripted simulation
  - `X` — exit

- **Three Allocation Strategies:**
  - **First-Fit:** Allocates the first hole large enough
  - **Best-Fit:** Chooses the smallest available hole
  - **Worst-Fit:** Selects the largest available hole

- **Dynamic Region Tracking:**  
  Free and allocated regions stored in a growing array of `region_t` structs, kept sorted by start address.

- **Automatic Hole Merging:**  
  When blocks are released, adjacent holes are coalesced to prevent unnecessary fragmentation.

- **Compaction Support:**  
  The `C` command shifts allocated blocks to the beginning of memory to eliminate external fragmentation.

- **Simulation Mode (Enhanced Feature):**  
  Batch command execution from a text file using:
  ```
  SIM trace.txt
  ```

## Implementation Details

### Core Data Structure
```c
typedef struct {
    int start;
    int size;
    int is_free;
    char pid[32];
} region_t;
```

### Key Functions
- `cmd_request()` — allocate with F/B/W strategy
- `cmd_release()` — free a block and merge holes
- `cmd_compact()` — rebuilds region list into one dense segment
- `cmd_stat()` — reports memory layout, totals, and fragmentation
- `cmd_stat V()` — prints a visual representation
- `cmd_sim()` — runs scripted input in batch mode

### Fragmentation Metrics
- **External Fragmentation:**
  ```
  (1 - largest_hole / total_free) * 100
  ```
- **Average Hole Size:**
  ```
  total_free / number_of_holes
  ```

### Example Visual Output
```
[##########....####......................]
^0                                 ^100 KB
```

## Usage

### Compile
```
gcc allocator.c -Wall -Wextra -o allocator
```

### Run
```
./allocator <memory_size>
```

Example:
```
./allocator 1048576
```

### Example Commands
```
allocator> RQ P1 40000 F
allocator> RQ P2 50000 B
allocator> RL P1
allocator> RQ P3 60000 W
allocator> STAT
allocator> C
allocator> STAT V
allocator> X
```

### Example SIM Script (`trace.txt`)
```
RQ P1 40000 F
RQ P2 50000 B
RL P1
RQ P3 60000 W
STAT V
X
```

Run it:
```
allocator> SIM trace.txt
```

## Error Handling

The simulator detects:
- Invalid or missing arguments
- Negative or zero size requests
- Invalid strategy characters
- Duplicate PID requests
- Releasing unknown processes
- Requests too large for memory

Examples:
```
Error: Process P2 already has allocated memory
Error: Invalid size value in RQ command
Error: Invalid strategy character in RQ command. Use F, B, or W.
Error: Not enough memory to allocate 1000 KB for process P5
```

## Notes
- Memory sizes are interpreted as KB
- Allocation is single contiguous region per process
- Compaction maintains process order
- Internal fragmentation is not modeled