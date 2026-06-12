#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>      // O_CREAT, O_EXCL
#include <sys/stat.h>   // mode constants for sem_open

static sem_t *cpu_done = NULL; // posted by a process after it runs one CPU tick
static char cpu_done_name[64] = {0};

/* helpers for scheduler-wide synchronization */
static int init_scheduler_sync(void){
    // create a unique name per run
    snprintf(cpu_done_name, sizeof(cpu_done_name), "/cpu_done_%d", (int)getpid());
    sem_unlink(cpu_done_name); // ensure no stale instance
    cpu_done = sem_open(cpu_done_name, O_CREAT | O_EXCL, 0600, 0);
    if (cpu_done == SEM_FAILED) {
        perror("sem_open cpu_done");
        cpu_done = NULL;
        return -1;
    }
    return 0;
}
static void destroy_scheduler_sync(void){
    if (cpu_done && cpu_done != SEM_FAILED) sem_close(cpu_done);
    if (cpu_done_name[0]) sem_unlink(cpu_done_name);
    cpu_done = NULL;
    cpu_done_name[0] = '\0';
}

/* CLI HANDLING */

/* config structure for CLI options */
struct config {
    int algorithm;          // scheduling algorithm (0=FCFS, 1=SJF, 2=RR, 3=Priority)
    int quantum;            // time quantum for RR
    char input_file[256];   // input file for process list
};

enum { ALG_FCFS = 0, ALG_SJF, ALG_RR, ALG_PRIORITY };

/* print usage instructions */
static void print_usage(const char *progname){
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "-f, --fcfs                Use FCFS scheduling (default)\n"
        "-s, --sjf                 Use SJF scheduling\n"
        "-r, --rr                  Use Round Robin scheduling\n"
        "-p, --priority            Use Priority scheduling\n"
        "-q, --quantum <N>         Time quantum for RR (default: 2)\n"
        "-i, --input <FILE>        Input CSV filename\n"
        "-h, --help                Show this help message\n",
        progname);
}

/* parse command-line arguments */
static void parse_args(int argc, char **argv, struct config *cfg){
    // set defaults
    cfg->algorithm = ALG_FCFS;
    cfg->quantum = 2;
    strcpy(cfg->input_file, "");

    // define long options
    static struct option long_opts[] = {
        {"fcfs",       no_argument,       0, 'f'},
        {"sjf",        no_argument,       0, 's'},
        {"rr",         no_argument,       0, 'r'},
        {"priority",   no_argument,       0, 'p'},
        {"quantum",    required_argument, 0, 'q'},
        {"input",      required_argument, 0, 'i'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int option;
    while ((option = getopt_long(argc, argv, "fsrpq:i:h", long_opts, NULL)) != -1) {
        switch (option) {
            case 'f': cfg->algorithm = ALG_FCFS; break;
            case 's': cfg->algorithm = ALG_SJF; break;
            case 'r': cfg->algorithm = ALG_RR; break;
            case 'p': cfg->algorithm = ALG_PRIORITY; break;
            case 'q': cfg->quantum = atoi(optarg); break;
            case 'i': strncpy(cfg->input_file, optarg, sizeof(cfg->input_file) - 1); break;
            case 'h': 
                print_usage(argv[0]); 
                exit(0);
                break;
            case '?':
                // getopt_long already printed an error message
                print_usage(argv[0]);
                exit(1);
                break;
            default:
                abort();
        }
    }

    // validate input file
    if (strlen(cfg->input_file) == 0){
        fprintf(stderr, "Error: Input file must be specified with -i or --input\n");
        print_usage(argv[0]);
        exit(1);
    }

    // validate quantum for RR
    if (cfg->algorithm == ALG_RR && cfg->quantum <= 0){
        fprintf(stderr, "Error: Quantum must be a positive integer for Round Robin scheduling\n");
        print_usage(argv[0]);
        exit(1);
    }
}

/* DEFS AND CSV LOADER */

/* per-process structure */
typedef struct {
    char pid[32];       // process ID
    int arrival;        // arrival time
    int burst;          // burst time
    int priority;       // priority

    int remaining;          // remaining CPU time for RR
    int start_time;         // first time scheduled
    int finish_time;        // time finished/completed
    int waiting_time;       // total waiting time in READY
    int response_time;      // start_time - arrival
    int turnaround_time;    // finish_time - arrival
    int started;            // flag: first run recorded?
    int in_ready;           // flag: currently enqueued in READY
    int completed;          // flag: finished execution

    sem_t *sem;         // private named semaphore for process
    char sem_name[64];  // semaphore name for unlinking
    pthread_t tid;      // thread handle
} process_t;

/* static function prototypes */
static int load_processes(const char *path, process_t **out_list, size_t *out_count);
static void trim(char *str);

/* threading and ready queue */
typedef struct ready_queue ready_queue_t;                           // forward declaration
static int rq_init(ready_queue_t *rq, size_t capacity);            // initialize ready queue
static void rq_destroy(ready_queue_t *rq);                          // destroy ready queue
static int rq_empty(const ready_queue_t *rq);                       // check if empty
static int rq_push(ready_queue_t *rq, int index);                   // push process index
static int rq_pop(ready_queue_t *rq);                               // pop process index
static int rq_remove(ready_queue_t *rq, int index);                 // remove specific process index
static void *process_thread(void *arg);                             // process thread function
static int spawn_process_threads(process_t *list, size_t count);    // spawn all process threads
static void join_and_cleanup(process_t *list, size_t count);        // join threads and cleanup

/* trim whitespace in-place */
static void trim(char *str){
    if (!str) return;

    // trim leading
    char *p = str;
    while (*p != '\0' && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p != str) memmove(str, p, strlen(p) + 1);

    // trim trailing
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\n' || str[len - 1] == '\r')){
        str[len - 1] = '\0';
        len--;
    }
}

/* load processes from CSV file */
static int load_processes(const char *path, process_t **out_list, size_t *out_count){
    FILE *fp = fopen(path, "r");
    if (!fp){
        fprintf(stderr, "Error: Unable to open input file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    char line[512];
    size_t capacity = 16;
    size_t count = 0;
    process_t *list = malloc(capacity * sizeof(process_t));
    if (!list){
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(fp);
        return -1;
    }

    // read lines
    int header_skipped = 0;
    while (fgets(line, sizeof(line), fp)){
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue; // skip empty lines

        // tokenize by commas
        char *tokens[4] = {0};
        char *ptr = line;
        int i = 0;
        while (i < 4 && ptr){
            tokens[i] = strsep(&ptr, ",");
            i++;
        }
        if (i < 4){
            fprintf(stderr, "Warning: Malformed line skipped: %s\n", line);
            continue;
        }

        // trim tokens
        for (int j = 0; j < 4; j++) trim(tokens[j]);

        // skip header if second token is not a number
        if (!header_skipped){
            char *endptr = NULL;
            strtol(tokens[1], &endptr, 10);
            if (endptr == tokens[1] || *endptr != '\0'){
                header_skipped = 1;
                continue; // skip header line
            }
            header_skipped = 1;
        }

        // parse fields
        char *pid = tokens[0];
        char *endptr = NULL;

        long arrival = strtol(tokens[1], &endptr, 10);
        if (endptr == tokens[1] || *endptr != '\0' || arrival < 0){
            fprintf(stderr, "Warning: Invalid arrival time in line skipped: %s\n", line);
            continue;
        }

        long burst = strtol(tokens[2], &endptr, 10);
        if (endptr == tokens[2] || *endptr != '\0' || burst <= 0){
            fprintf(stderr, "Warning: Invalid burst time in line skipped: %s\n", line);
            continue;
        }

        long priority = strtol(tokens[3], &endptr, 10);
        if (endptr == tokens[3] || *endptr != '\0' || priority < 0){
            fprintf(stderr, "Warning: Invalid priority in line skipped: %s\n", line);
            continue;
        }

        // expand list if needed
        if (count == capacity){
            capacity *= 2;
            process_t *tmp = realloc(list, capacity * sizeof(process_t));
            if (!tmp){
                fprintf(stderr, "Error: Memory allocation failed\n");
                free(list);
                fclose(fp);
                return -1;
            }
            list = tmp;
        }

        // fill process struct
        process_t *proc = &list[count++];
        strncpy(proc->pid, pid, sizeof(proc->pid) - 1);
        proc->pid[sizeof(proc->pid) - 1] = '\0';
        proc->arrival = (int)arrival;
        proc->burst = (int)burst;
        proc->priority = (int)priority;

        proc->remaining = proc->burst;
        proc->start_time = -1;
        proc->finish_time = -1;
        proc->waiting_time = 0;
        proc->response_time = -1;
        proc->turnaround_time = -1;
        proc->started = 0;
        proc->in_ready = 0;
        proc->completed = 0;

        // create a unique named semaphore for this process
        snprintf(proc->sem_name, sizeof(proc->sem_name), "/proc_%d_%zu", (int)getpid(), count - 1);
        sem_unlink(proc->sem_name);
        proc->sem = sem_open(proc->sem_name, O_CREAT | O_EXCL, 0600, 0);
        if (proc->sem == SEM_FAILED){
            fprintf(stderr, "Error: sem_open failed for pid '%s': %s\n", proc->pid, strerror(errno));
            // cleanup semaphores already initialized
            for (size_t k = 0; k < count - 1; ++k) {
                if (list[k].sem && list[k].sem != SEM_FAILED) sem_close(list[k].sem);
                if (list[k].sem_name[0]) sem_unlink(list[k].sem_name);
            }
            free(list);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);
    *out_list = list;
    *out_count = count;
    return 0;
}

/* READY QUEUE + THREADING */

struct ready_queue {
    int *buf;           // buffer of process indices
    size_t head;        // head index (next to pop)
    size_t tail;        // tail index (next to push)
    size_t size;        // current size
    size_t capacity;    // maximum capacity
};

static int rq_init(ready_queue_t *rq, size_t capacity){
    rq->buf = (int *)malloc(capacity * sizeof(int));
    if (!(rq)->buf) return -1;
    rq->head = rq->tail = rq->size = 0;
    rq->capacity = capacity;
    return 0;
}

static void rq_destroy(ready_queue_t *rq){
    free(rq->buf);
    rq->buf = NULL;
    rq->head = rq->tail = rq->size = rq->capacity = 0;
}

static int rq_empty(const ready_queue_t *rq){
    return rq->size == 0;
}

static int rq_push(ready_queue_t *rq, int index){
    if (rq->size == rq->capacity) return -1; // full
    rq->buf[rq->tail] = index;
    rq->tail = (rq->tail + 1) % rq->capacity;
    rq->size++;
    return 0;
}

static int rq_pop(ready_queue_t *rq){
    if (rq_empty(rq)) return -1; // empty
    int v = rq->buf[rq->head];
    rq->head = (rq->head + 1) % rq->capacity;
    rq->size--;
    return v;
}

static int rq_remove(ready_queue_t *rq, int index){
    if (rq_empty(rq)) return -1; // empty
    int *tmp = (int *)malloc(rq->capacity * sizeof(int));
    if (!tmp) return -1;

    size_t write = 0;
    int removed = 0;
    for (size_t i = 0; i < rq->size; i++){
        int v = rq->buf[(rq->head + i) % rq->capacity];
        if (!removed && v == index){
            removed = 1;          // skip this one
            continue;
        }
        tmp[write++] = v;
    }

    if (removed){
        // rebuild queue from head with compacted contents
        memcpy(rq->buf, tmp, write * sizeof(int));
        rq->head = 0;
        rq->size = write;
        rq->tail = write % rq->capacity;
    }

    free(tmp);
    return removed ? 0 : -1;
}

static void *process_thread(void *arg){
    process_t *proc = (process_t *)arg;

    for (;;){
        // block until the scheduler dispatches this process
        if (sem_wait(proc->sem) != 0) continue;
        if (proc->remaining <= 0){
            // safety: if scheduled after completion, just exit
            sem_post(cpu_done);
            break;
        }

        // simulate one CPU tick
        proc->remaining--;

        // signal scheduler that CPU tick is done
        sem_post(cpu_done);
        if(proc->remaining == 0){
            break; // process completed
        }
    }

    return NULL;
}

static int spawn_process_threads(process_t *list, size_t count){
    for (size_t i = 0; i < count; i++){
        if (pthread_create(&list[i].tid, NULL, process_thread, &list[i]) != 0){
            fprintf(stderr, "Error: pthread_create failed for pid '%s': %s\n", list[i].pid, strerror(errno));
            
            // cancel + join threads already created (they may be blocked in sem_wait)
            for (size_t j = 0; j < i; j++){
                pthread_cancel(list[j].tid);
                pthread_join(list[j].tid, NULL);
            }
            return -1;
        }
    }
    return 0;
}

static void join_and_cleanup(process_t *list, size_t count){
    for (size_t i = 0; i < count; i++){
        if (list[i].tid) pthread_join(list[i].tid, NULL);
        if (list[i].sem && list[i].sem != SEM_FAILED) sem_close(list[i].sem);
        if (list[i].sem_name[0]) sem_unlink(list[i].sem_name);
    }
}

#if 1
/* SIMULATION STATE + HELPERS */
typedef struct {
    int clock_time;         // simulation time
    int cpu_busy_time;      // ticks CPU was busy
    int finished_count;     // number of finished processes
    long sum_wait;
    long sum_resp;
    long sum_turn;
} sim_t;

typedef struct { int t0, t1, idx; } gantt_slice_t;
typedef struct { gantt_slice_t *v; size_t n, cap; int current_idx; int current_t0; } gantt_t;

static int gantt_init(gantt_t *g){ g->v=NULL; g->n=0; g->cap=0; g->current_idx=-2; g->current_t0=0; return 0; }
static void gantt_push(gantt_t *g, int t0, int t1, int idx){
    if (t1 <= t0) return;
    if (g->n == g->cap){ size_t nc = g->cap? g->cap*2:16; g->v = (gantt_slice_t*)realloc(g->v, nc*sizeof(*g->v)); if(!g->v){ g->n=g->cap=0; return; } g->cap=nc; }
    g->v[g->n++] = (gantt_slice_t){ t0,t1,idx };
}
static void gantt_switch(gantt_t *g, int now, int next_idx){
    if (g->current_idx == -2){ g->current_idx = next_idx; g->current_t0 = now; return; }
    if (g->current_idx != next_idx){
        gantt_push(g, g->current_t0, now, g->current_idx);
        g->current_idx = next_idx;
        g->current_t0 = now;
    }
}
static void gantt_close(gantt_t *g, int now){
    if (g->current_idx != -2){ gantt_push(g, g->current_t0, now, g->current_idx); g->current_idx = -2; }
}
static void gantt_free(gantt_t *g){ free(g->v); g->v=NULL; g->n=g->cap=0; }

static void admit_arrivals(int now, process_t *ps, size_t n, ready_queue_t *rq){
    for (size_t i=0;i<n;i++){
        // admit a process only at the exact arrival tick
        if (!ps[i].completed && !ps[i].in_ready && ps[i].remaining > 0 && ps[i].arrival == now){
            if (rq_push(rq, (int)i) == 0){
                ps[i].in_ready = 1; // now in READY once, no duplicate enqueues later
            }
        }
    }
}
static void tick_waiting(const ready_queue_t *rq, process_t *ps){
    // increment waiting time for all indices currently in READY
    for (size_t k=0; k<rq->size; k++){
        int idx = rq->buf[(rq->head + k) % rq->capacity];
        ps[idx].waiting_time++;
    }
}
static int all_finished(process_t *ps, size_t n){
    for (size_t i=0;i<n;i++) if (!ps[i].completed) return 0; return 1;
}

/* selection helpers */
static int rq_pop_min_remaining(ready_queue_t *rq, process_t *ps){
    if (rq->size==0) return -1;
    int best_pos=-1; int best_idx=-1; int best_rem=0x7fffffff;
    for (size_t k=0; k<rq->size; k++){
        int idx = rq->buf[(rq->head + k) % rq->capacity];
        if (ps[idx].remaining < best_rem){ best_rem = ps[idx].remaining; best_idx = idx; best_pos = (int)k; }
    }
    // remove best_pos
    int removed = rq->buf[(rq->head + best_pos) % rq->capacity];
    rq_remove(rq, removed);
    return best_idx;
}
static int rq_peek_best_priority(const ready_queue_t *rq, process_t *ps){
    if (rq->size==0) return -1;
    int best=-1; int bestp=0x7fffffff;
    for (size_t k=0;k<rq->size;k++){
        int idx = rq->buf[(rq->head + k) % rq->capacity];
        if (ps[idx].priority < bestp){ bestp = ps[idx].priority; best = idx; }
    }
    return best;
}
static int rq_pop_specific(ready_queue_t *rq, int idx){ return rq_remove(rq, idx)==0 ? idx : -1; }

/* METRICS + PRINTING */


// pretty gantt to match the assignment example formatting
static void print_gantt_pretty(const gantt_t *g, process_t *ps){
    if (!g || g->n == 0) return;

    // collect only non-idle slices
    // note: fcfs/sjf on our workloads do not generate idle, but handle it anyway
    int bounds_cap = (int)g->n + 1;
    int *b = (int *)malloc(sizeof(int) * (bounds_cap));
    int  *idxs = (int *)malloc(sizeof(int) * g->n);
    int m = 0; // number of visible slices (non-idle)

    for (size_t i = 0; i < g->n; ++i){
        const gantt_slice_t *s = &g->v[i];
        if (s->idx < 0) continue; // skip idle in pretty view
        if (m == 0){
            b[0] = s->t0; // first boundary
        }
        idxs[m] = s->idx; // store process index for the cell
        b[m+1] = s->t1;   // next boundary
        m++;
    }
    if (m == 0){ free(b); free(idxs); return; }

    // choose a scale so widths look readable
    // 2 chars per unit time works well for small examples
    const int SCALE = 2;

    // line 1: the boundary times across the top
    // print first time, then pad proportionally to the gap before printing the next
    for (int i = 0; i <= m; ++i){
        if (i == 0){
            printf("%d", b[i]);
        } else {
            int gap = (b[i] - b[i-1]) * SCALE;
            for (int s = 0; s < gap; ++s) putchar(' ');
            printf("%d", b[i]);
        }
        if (i < m) putchar(' '); // small spacer between numbers
    }
    putchar('\n');

    // line 2: top border segments
    for (int i = 0; i < m; ++i){
        int w = (b[i+1] - b[i]) * SCALE;
        putchar('|');
        for (int s = 0; s < w; ++s) putchar('-');
    }
    printf("|\n");

    // line 3: labels centered in each segment
    for (int i = 0; i < m; ++i){
        int w = (b[i+1] - b[i]) * SCALE;
        putchar('|');
        const char *name = ps[idxs[i]].pid;
        int name_len = (int)strlen(name);
        int pad = w - name_len;
        if (pad < 0) pad = 0; // truncate visually if too small
        int left = pad/2;
        int right = pad - left;
        for (int s = 0; s < left; ++s) putchar(' ');
        fputs(name, stdout);
        for (int s = 0; s < right; ++s) putchar(' ');
    }
    printf("|\n");

    free(b); free(idxs);
}
static void finalize_process(sim_t *S, process_t *p, int now){
    p->finish_time = now;
    p->turnaround_time = p->finish_time - p->arrival;
    if (p->response_time < 0 && p->start_time >= 0) p->response_time = p->start_time - p->arrival;
    S->sum_wait += p->waiting_time;
    S->sum_resp += (p->start_time >= 0 ? p->start_time - p->arrival : 0);
    S->sum_turn += p->turnaround_time;
    p->completed = 1;
    S->finished_count++;
}

static void print_results(const char *title, process_t *ps, size_t n, const gantt_t *g, const sim_t *S){
    printf("===== %s Scheduling =====\n", title);
    // Gantt Timeline (assignment style)
    printf("Timeline (Gantt Chart):\n");
    print_gantt_pretty(g, ps);
    printf("-------------------------------------\n");
    printf("PID   Arr  Burst  Pri  Start  Finish  Wait  Resp  Turn\n");
    for (size_t i=0;i<n;i++){
        printf("%-5s %4d %6d %4d %6d %7d %5d %5d %5d\n",
               ps[i].pid, ps[i].arrival, ps[i].burst, ps[i].priority,
               ps[i].start_time, ps[i].finish_time, ps[i].waiting_time,
               ps[i].response_time, ps[i].turnaround_time);
    }
    printf("-------------------------------------\n");
    double nproc = (double)n;
    double avgW = (n? (double)S->sum_wait / nproc : 0.0);
    double avgR = (n? (double)S->sum_resp / nproc : 0.0);
    double avgT = (n? (double)S->sum_turn / nproc : 0.0);
    double thru = (S->clock_time>0? (double)S->finished_count / (double)S->clock_time : 0.0);
    double util = (S->clock_time>0? (double)S->cpu_busy_time * 100.0 / (double)S->clock_time : 0.0);
    printf("Avg Wait = %.2f\nAvg Resp = %.2f\nAvg Turn = %.2f\nThroughput = %.2f jobs/unit time\nCPU Utilization = %.0f%%\n",
           avgW, avgR, avgT, thru, util);
}

/* ALGORITHMS */
static void run_fcfs(process_t *ps, size_t n){
    sim_t S = {0}; gantt_t G; gantt_init(&G);
    ready_queue_t rq; rq_init(&rq, n? n*2: 2);

    int current = -1;
    init_scheduler_sync();

    while (!all_finished(ps,n)){
        admit_arrivals(S.clock_time, ps, n, &rq);

        // choose if CPU idle
        if (current == -1){
            current = rq_pop(&rq);
            if (current != -1){ ps[current].in_ready = 0; if(!ps[current].started){ ps[current].started=1; ps[current].start_time = S.clock_time; ps[current].response_time = ps[current].start_time - ps[current].arrival; }
                gantt_switch(&G, S.clock_time, current);
            } else {
                gantt_switch(&G, S.clock_time, -1);
            }
        }

        // run one tick
        if (current != -1){
            sem_post(ps[current].sem);
            sem_wait(cpu_done);
            S.cpu_busy_time++;
        }

        // waiting time for others
        tick_waiting(&rq, ps);

        // advance time
        S.clock_time++;

        // check completion
        if (current != -1 && ps[current].remaining == 0){
            finalize_process(&S, &ps[current], S.clock_time);
            current = -1;
        }
    }

    gantt_close(&G, S.clock_time);
    print_results("FCFS", ps, n, &G, &S);
    rq_destroy(&rq); destroy_scheduler_sync(); gantt_free(&G);
}

static void run_sjf(process_t *ps, size_t n){
    sim_t S = {0}; gantt_t G; gantt_init(&G);
    ready_queue_t rq; rq_init(&rq, n? n*2:2);
    int current = -1;
    init_scheduler_sync();

    while (!all_finished(ps,n)){
        admit_arrivals(S.clock_time, ps, n, &rq);
        if (current == -1){
            current = rq_pop_min_remaining(&rq, ps);
            if (current != -1){ ps[current].in_ready=0; if(!ps[current].started){ ps[current].started=1; ps[current].start_time=S.clock_time; ps[current].response_time = ps[current].start_time - ps[current].arrival; }
                gantt_switch(&G, S.clock_time, current);
            } else { gantt_switch(&G, S.clock_time, -1); }
        }

        if (current != -1){
            sem_post(ps[current].sem);
            sem_wait(cpu_done);
            S.cpu_busy_time++;
        }

        tick_waiting(&rq, ps);
        S.clock_time++;

        if (current != -1 && ps[current].remaining == 0){
            finalize_process(&S, &ps[current], S.clock_time);
            current = -1;
        }
    }

    gantt_close(&G, S.clock_time);
    print_results("SJF", ps, n, &G, &S);
    rq_destroy(&rq); destroy_scheduler_sync(); gantt_free(&G);
}

static void run_rr(process_t *ps, size_t n, int quantum){
    sim_t S = {0}; gantt_t G; gantt_init(&G);
    ready_queue_t rq; rq_init(&rq, n? n*2:2);
    int current = -1; int slice = 0; int to_requeue = -1;
    init_scheduler_sync();

    while (!all_finished(ps,n)){
        admit_arrivals(S.clock_time, ps, n, &rq);
        // if last tick preempted a process, enqueue it *after* admitting arrivals
        if (to_requeue != -1){
            rq_push(&rq, to_requeue);
            ps[to_requeue].in_ready = 1;
            to_requeue = -1;
        }

        if (current == -1){
            current = rq_pop(&rq);
            if (current != -1){ ps[current].in_ready=0; if(!ps[current].started){ ps[current].started=1; ps[current].start_time=S.clock_time; ps[current].response_time = ps[current].start_time - ps[current].arrival; }
                slice = quantum; gantt_switch(&G, S.clock_time, current);
            } else { gantt_switch(&G, S.clock_time, -1); }
        }

        if (current != -1){
            sem_post(ps[current].sem);
            sem_wait(cpu_done);
            S.cpu_busy_time++;
            slice--;
        }

        tick_waiting(&rq, ps);
        S.clock_time++;

        if (current != -1 && ps[current].remaining == 0){
            finalize_process(&S, &ps[current], S.clock_time);
            current = -1; slice = 0;
        } else if (current != -1 && slice == 0){
            // time slice expired: mark to requeue on next loop, after admitting arrivals
            to_requeue = current;
            current = -1; slice = 0;
        }
    }

    gantt_close(&G, S.clock_time);
    print_results("RR", ps, n, &G, &S);
    rq_destroy(&rq); destroy_scheduler_sync(); gantt_free(&G);
}

static void run_priority(process_t *ps, size_t n){
    sim_t S = {0}; gantt_t G; gantt_init(&G);
    ready_queue_t rq; rq_init(&rq, n? n*2:2);
    int current = -1;
    init_scheduler_sync();

    while (!all_finished(ps,n)){
        admit_arrivals(S.clock_time, ps, n, &rq);

        // preemptive: if a higher priority (lower number) exists in READY, switch
        int best_ready = rq_peek_best_priority(&rq, ps);
        if (current == -1){
            if (best_ready != -1){ rq_pop_specific(&rq, best_ready); ps[best_ready].in_ready=0; current = best_ready; if(!ps[current].started){ ps[current].started=1; ps[current].start_time=S.clock_time; ps[current].response_time = ps[current].start_time - ps[current].arrival; } gantt_switch(&G, S.clock_time, current); }
            else { gantt_switch(&G, S.clock_time, -1); }
        } else {
            if (best_ready != -1 && ps[best_ready].priority < ps[current].priority){
                // preempt current
                rq_push(&rq, current); ps[current].in_ready=1;
                rq_pop_specific(&rq, best_ready); ps[best_ready].in_ready=0; current = best_ready;
                if(!ps[current].started){ ps[current].started=1; ps[current].start_time=S.clock_time; ps[current].response_time = ps[current].start_time - ps[current].arrival; }
                gantt_switch(&G, S.clock_time, current);
            }
        }

        if (current != -1){
            sem_post(ps[current].sem);
            sem_wait(cpu_done);
            S.cpu_busy_time++;
        }

        tick_waiting(&rq, ps);
        S.clock_time++;

        if (current != -1 && ps[current].remaining == 0){
            finalize_process(&S, &ps[current], S.clock_time);
            current = -1;
        }
    }

    gantt_close(&G, S.clock_time);
    print_results("Priority", ps, n, &G, &S);
    rq_destroy(&rq); destroy_scheduler_sync(); gantt_free(&G);
}

/* DISPATCH + MAIN */
static void run_scheduler(const struct config *cfg, process_t *ps, size_t n){
    switch (cfg->algorithm){
        case ALG_FCFS:    run_fcfs(ps, n); break;
        case ALG_SJF:     run_sjf(ps, n); break;
        case ALG_RR:      run_rr(ps, n, cfg->quantum); break;
        case ALG_PRIORITY:run_priority(ps, n); break;
        default:          run_fcfs(ps, n); break;
    }
}

int main(int argc, char **argv){
    struct config cfg; parse_args(argc, argv, &cfg);
    process_t *procs = NULL; size_t nprocs = 0;
    if (load_processes(cfg.input_file, &procs, &nprocs) != 0){ return 1; }
    if (spawn_process_threads(procs, nprocs) != 0){ free(procs); return 1; }

    run_scheduler(&cfg, procs, nprocs);

    join_and_cleanup(procs, nprocs);
    free(procs);
    return 0;
}
#endif
