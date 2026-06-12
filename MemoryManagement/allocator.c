#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

/* GLOBAL CONFIG + DATA STRUCTURES */

typedef struct {
    int start;      // start address of region
    int size;       // size of region
    int is_free;       // 1 if free, 0 if allocated
    char pid[32];   // process ID or name if allocated
} region_t;

/* global memory state */
static region_t  *regions = NULL;
static size_t     region_count = 0;
static size_t     region_capacity = 0;
static int        total_memory_size = 0;    // MAX memory size

/* allocation strat */
typedef enum {
    INVALID_STRAT = -1,
    FIRST_FIT = 0,
    BEST_FIT,
    WORST_FIT
} fit_strategy_t;

static void init_memory(int total_size);
static void cleanup_memory(void);

static void ensure_region_capacity(size_t need);
static void insert_region(size_t idx, region_t r);
static void remove_region(size_t idx);
static void merge_adjacent_holes(void);

static int find_region_by_pid(const char *pid);

static void cmd_request(const char *pid, int size, fit_strategy_t strategy);
static void cmd_release(const char *pid);
static void cmd_compact(void);
static void cmd_status(int verbose);
static void cmd_sim(const char *filename);

static void print_prompt(void);
static int handle_command(const char *line);

/* small helpers */
static void trim(char *s);
static fit_strategy_t parse_strategy_char(char c);
static int str_to_int(const char *s, int *out);

/* MEMORY INIT + HELPERS */

/* initialize mem w/ a single hole */
static void init_memory(int total_size){
    total_memory_size = total_size;

    region_count = 0;
    region_capacity = 8;
    regions = (region_t*)malloc(region_capacity * sizeof(region_t));
    if (!regions){
        fprintf(stderr, "Error: Memory allocation failed during init_memory\n");
        exit(1);
    }

    regions[0].start = 0;
    regions[0].size = total_memory_size;
    regions[0].is_free = 1;
    regions[0].pid[0] = '\0';
    region_count = 1;
}

/* free global region array */
static void cleanup_memory(void){
    free(regions);
    regions = NULL;
    region_count = 0;
    region_capacity = 0;
    total_memory_size = 0;
}

/* make array bigger if needed */
static void ensure_region_capacity(size_t need){
    if (need <= region_capacity) return;

    size_t new_capacity = region_capacity ? region_capacity * 2 : 8;
    if (new_capacity < need) new_capacity = need;
    region_t *tmp = (region_t *)realloc(regions, new_capacity * sizeof(region_t));
    if (!tmp){
        fprintf(stderr, "Error: Memory allocation failed during ensure_region_capacity\n");
        exit(1);
    }
    regions = tmp;
    region_capacity = new_capacity;
}

/* insert region r at idx, shift to right */
static void insert_region(size_t idx, region_t r){
    ensure_region_capacity(region_count + 1);
    if (idx > region_count) idx = region_count;
    for (size_t i = region_count; i > idx; --i){
        regions[i] = regions[i - 1];
    }
    regions[idx] = r;
    region_count++;
}

/* remove region at idx */
static void remove_region(size_t idx){
    if (idx >= region_count) return;
    for (size_t i = idx; i + 1 < region_count; ++i){
        regions[i] = regions[i + 1];
    }
    region_count--;
}

/* merge adjacent free regions */
static void merge_adjacent_holes(void){
    if (region_count == 0) return;

    size_t i = 0;
    while (i + 1 < region_count){
        region_t *curr = &regions[i];
        region_t *next = &regions[i + 1];
        if (curr->is_free && next->is_free && (curr->start + curr->size == next->start)){
            // merge
            curr->size += next->size;
            remove_region(i + 1);
        } else {
            i++;
        }
    }
}


/* find region index by pid */
static int find_region_by_pid(const char *pid){
    for (size_t i = 0; i < region_count; i++){
        if (!regions[i].is_free && strcmp(regions[i].pid, pid) == 0){
            return (int)i;
        }
    }
    return -1;
}

/* trim whitespace */
static void trim(char *s){
    if (!s) return;

    // trim leading
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);

    // trim trailing
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])){
        s[len - 1] = '\0';
        len--;
    }
}

/* safe int parse */
static int str_to_int(const char *s, int *out){
    char *endptr;
    long val = strtol(s, &endptr, 10);
    if (endptr == s || *endptr != '\0' || val < 0 || val > INT_MAX){
        return -1; // failure
    }
    *out = (int)val;
    return 0; // success
}

/* MEMORY ALLOCATION + RELEASE */

/* find hole index based on strategy */
static int find_hole_index(int size, fit_strategy_t strategy){
    int best_idx = -1;
    int best_size = 0;

    for (size_t i = 0; i < region_count; i++){
        if (!regions[i].is_free || regions[i].size < size) continue;

        if (strategy == FIRST_FIT){
            return (int)i;
        } else if (strategy == BEST_FIT){
            if (best_idx == -1 || regions[i].size < best_size){
                best_idx = (int)i;
                best_size = regions[i].size;
            }
        } else if (strategy == WORST_FIT){
            if (best_idx == -1 || regions[i].size > best_size){
                best_idx = (int)i;
                best_size = regions[i].size;
            }
        }
    }
    return best_idx;
}

/* map strat char to F/B/W enum */
static fit_strategy_t parse_strategy_char(char c){
    switch (toupper((unsigned char)c)){
        case 'F': return FIRST_FIT;
        case 'B': return BEST_FIT;
        case 'W': return WORST_FIT;
        default:  return -1; // invalid
    }
}

/* handle request command */
static void cmd_request(const char *pid, int size, fit_strategy_t strategy){
    if (size <= 0){
        printf("Error: Request size must be positive\n");
        return;
    }
    if (find_region_by_pid(pid) != -1){
        printf("Error: Process %s already has allocated memory\n", pid);
        return;
    }

    int hole_idx = find_hole_index(size, strategy);
    if (hole_idx < 0){
        printf("Error: Not enough memory to allocate %d KB for process %s\n", size, pid);
        return;
    }

    region_t hole = regions[hole_idx];
    if (hole.size == size){
        // allocate entire hole
        regions[hole_idx].is_free = 0;
        strncpy(regions[hole_idx].pid, pid, sizeof(regions[hole_idx].pid) - 1);
        regions[hole_idx].pid[sizeof(regions[hole_idx].pid) - 1] = '\0';
    } else {
        // split hole
        region_t new_region;
        new_region.start = hole.start;
        new_region.size = size;
        new_region.is_free = 0;
        strncpy(new_region.pid, pid, sizeof(new_region.pid) - 1);
        new_region.pid[sizeof(new_region.pid) - 1] = '\0';

        // adjust existing hole to start after allocated region
        regions[hole_idx].start = hole.start + size;
        regions[hole_idx].size = hole.size - size;

        insert_region(hole_idx, new_region);
    }
}

/* handle release command */
static void cmd_release(const char *pid){
    int region_idx = find_region_by_pid(pid);
    if (region_idx < 0){
        printf("Error: Process %s has no allocated memory to release\n", pid);
        return;
    }

    regions[region_idx].is_free = 1;
    regions[region_idx].pid[0] = '\0';

    // merge adjacent holes
    merge_adjacent_holes();
}

/* COMPACTION + STATS */

/* compact memory by moving all allocated regions to the beginning, merge all free into one */
static void cmd_compact(void){
    if (region_count == 0) return;

    // create tmp copy of allocated regions
    region_t *allocated = (region_t *)malloc(region_count * sizeof(region_t));
    if (!allocated){
        fprintf(stderr, "Error: Memory allocation failed during compaction\n");
        return;
    }
    size_t alloc_count = 0;

    for (size_t i = 0; i < region_count; i++){
        if (!regions[i].is_free){
            allocated[alloc_count++] = regions[i];
        }
    }

    // rebuild regions array
    region_count = 0;

    int current_start = 0;
    for (size_t i = 0; i < alloc_count; i++){
        allocated[i].start = current_start;
        current_start += allocated[i].size;
        ensure_region_capacity(region_count + 1);
        regions[region_count++] = allocated[i];
    }

    // add single free region if space remains
    if (current_start < total_memory_size){
        region_t hole;
        hole.start = current_start;
        hole.size = total_memory_size - current_start;
        hole.is_free = 1;
        hole.pid[0] = '\0';
        ensure_region_capacity(region_count + 1);
        regions[region_count++] = hole;
    }

    free(allocated);
}

/* print visual bar representation of memory, 50 chars wide (2% of total memory each char) */
static void print_visual_map(void){
    const int WIDTH = 50;
    if (total_memory_size <= 0){
        printf("[No memory allocated]\n");
        return;
    }

    printf("[");
    for (int i = 0; i < WIDTH; i++){
        // sample midpoint of this segment
        int start = (i * total_memory_size) / WIDTH;
        int end = ((i + 1) * total_memory_size) / WIDTH;
        int mid = (start + end) / 2;

        // find which region this midpoint falls into
        char c = '.';
        for (size_t r = 0; r < region_count; r++){
            if (mid >= regions[r].start && mid < regions[r].start + regions[r].size){
                c = regions[r].is_free ? '.' : '#';
                break;
            }
        }
        putchar(c);
    }
    printf("]\n");
    printf("^0"); // start
    for (int i = 1; i < WIDTH - 4; i++) putchar(' ');
    printf("^%d KB\n", total_memory_size); // end
}

/* handle status command */
static void cmd_status(int verbose){
    int allocated_total = 0;
    int free_total = 0;
    int largest_hole = 0;
    int hole_count = 0;

    printf("\nAllocated memory:\n");
    for (size_t i = 0; i < region_count; i++){
        if (!regions[i].is_free){
            int start = regions[i].start;
            int end = regions[i].start + regions[i].size - 1;
            int size = regions[i].size;
            printf("  Process %s: Start = %d KB, End = %d KB, Size = %d KB\n", regions[i].pid, start, end, size);
            allocated_total += size;
        }
    }

    printf("\nFree memory:\n");
    for (size_t i = 0; i < region_count; i++){
        if (regions[i].is_free){
            int start = regions[i].start;
            int end = regions[i].start + regions[i].size - 1;
            int size = regions[i].size;
            printf("  Hole %d: Start = %d KB, End = %d KB, Size = %d KB\n", hole_count + 1, start, end, size);
            free_total += size;
            hole_count++;
            if (size > largest_hole) largest_hole = size;
        }
    }

    printf("\nSummary:\n");
    printf("  Total allocated: %d KB\n", allocated_total);
    printf("  Total free: %d KB\n", free_total);
    if (hole_count > 0){
        double external_frag = 0.0;
        if (free_total > 0){
            external_frag = (1.0 - (double)largest_hole / (double)free_total) * 100.0;
        }
        double avg_hole = (double)free_total / (double)hole_count;
        printf("  Largest hole: %d KB\n", largest_hole);
        printf("  External fragmentation: %.1f%%\n", external_frag);
        printf("  Average hole size: %.0f KB\n", avg_hole);
    } else {
        printf("No free memory holes.\n");
    }

    if (verbose){
        printf("\nVisual memory map:\n");
        print_visual_map();
    }

    printf("\n");
}

/* SIM MODE + COMMAND PARSING */
static void print_prompt(void){
    printf("allocator> ");
    fflush(stdout);
}

static void cmd_sim(const char *filename){
    FILE *file = fopen(filename, "r");
    if (!file){
        printf("Error: Can't open sim file %s\n", filename);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)){
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue; // skip empty or comment lines

        // echo command
        printf("allocator> %s\n", line);
        if (handle_command(line)){
            // X inside sim exits sim mode
            fclose(file);
            exit(0);
        }
    }

    fclose(file);
}

/* parse and execute commands 
 * return 1 if exit command, else 0
 */
static int handle_command(const char *line){
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim(buf);
    if (buf[0] == '\0') return 0;

    char *cmd = strtok(buf, " \t");
    if (!cmd) return 0;

    // uppercase command
    for (char *p = cmd; *p; p++) *p = toupper((unsigned char)*p);

    if (strcmp(cmd, "RQ") == 0){
        // RQ <pid> <size> <F/B/W>
        char *pid = strtok(NULL, " \t");
        char *size_str = strtok(NULL, " \t");
        char *strat_str = strtok(NULL, " \t");

        if (!pid || !size_str || !strat_str || strat_str[0] == '\0'){
            printf("Error: Invalid RQ command format. Usage: RQ <pid> <size> <F/B/W>\n");
            return 0;
        }

        int size;
        if (str_to_int(size_str, &size) != 0){
            printf("Error: Invalid size value in RQ command\n");
            return 0;
        }

        fit_strategy_t strategy = parse_strategy_char(strat_str[0]);
        if (strategy == -1){
            printf("Error: Invalid strategy character in RQ command. Use F, B, or W.\n");
            return 0;
        }
        cmd_request(pid, size, strategy);

    } else if (strcmp(cmd, "RL") == 0){
        // RL <pid>
        char *pid = strtok(NULL, " \t");
        if (!pid){
            printf("Error: Invalid RL command format. Usage: RL <pid>\n");
            return 0;
        }
        cmd_release(pid);

    } else if (strcmp(cmd, "C") == 0){
        // C
        cmd_compact();

    } else if (strcmp(cmd, "STAT") == 0){
        // STAT [V]
        char *arg = strtok(NULL, " \t");
        int verbose = 0;
        if (arg && (toupper((unsigned char)arg[0]) == 'V')){
            verbose = 1;
        }
        cmd_status(verbose);

    } else if (strcmp(cmd, "SIM") == 0){
        // SIM <filename>
        char *filename = strtok(NULL, " \t");
        if (!filename){
            printf("Error: Invalid SIM command format. Usage: SIM <filename>\n");
            return 0;
        }
        cmd_sim(filename);

    } else if (strcmp(cmd, "X") == 0){
        // X
        return 1; // exit

    } else {
        printf("Error: Unknown command '%s'\n", cmd);
    }

    return 0;
}

/* MAIN */
int main(int argc, char *argv[]){
    if (argc != 2){
        fprintf(stderr, "Usage: %s <total_memory_size>\n", argv[0]);
        return 1;
    }

    int total_size;
    if (str_to_int(argv[1], &total_size) != 0 || total_size <= 0){
        fprintf(stderr, "Error: Invalid total memory size '%s'\n", argv[1]);
        return 1;
    }

    init_memory(total_size);

    char line[256];
    for (;;){
        print_prompt();
        if (!fgets(line, sizeof(line), stdin)){
            printf("\n");
            break; // EOF
        }
        trim(line);
        if (handle_command(line)){
            break; // exit command
        }
    }

    cleanup_memory();
    return 0;
}