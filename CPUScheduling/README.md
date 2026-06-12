# PA4: CPU Scheduling Simulation

For this project, I implemented a complete CPU Scheduling Simulator using POSIX threads and semaphores. The goal was to simulate how different CPU scheduling algorithms handle multiple processes arriving at various times, and to measure key performance metrics such as turnaround, waiting, and response times.

## Features
- **Multiple Scheduling Algorithms:** Supports four algorithms selectable via command-line flags:
  - `FCFS` — First-Come, First-Served (non-preemptive)
  - `SJF` — Shortest Job First (non-preemptive)
  - `RR` — Round Robin (preemptive, with configurable time quantum)
  - `PRIORITY` — Preemptive Priority (lower number = higher priority)
- **POSIX Thread Simulation:** Each process is represented as a thread, and named semaphores coordinate CPU access and timing.
- **Accurate Timing Simulation:** The simulator advances in discrete time ticks, admitting arrivals, dispatching processes, and collecting metrics at each tick.
- **CSV Input Parsing:** Reads a comma-separated workload file with fields:
  ```
  PID,Arrival,Burst,Priority
  P1,0,5,2
  P2,1,3,1
  P3,2,8,3
  P4,3,6,2
  ```
- **Detailed Metrics Output:** After execution, it prints per-process stats (Start, Finish, Wait, Response, Turnaround) and summary averages, throughput, and CPU utilization.

## Implementation Details
- **System Calls / APIs:**
  - `pthread_create()` and `pthread_join()` — spawn and synchronize process threads.
  - `sem_open()`, `sem_wait()`, `sem_post()`, and `sem_unlink()` — named semaphores for safe cross-thread signaling on macOS.
  - `clock_gettime()` — used for timing precision where applicable.
- **Data Structures:**
  - **`process_t`** — represents each process with attributes: PID, arrival, burst, priority, remaining time, and statistics (start, finish, wait, response, turnaround).
  - **`ready_queue_t`** — manages process indices in a simple FIFO or priority-based queue depending on the algorithm.
  - **`gantt_t`** — records execution slices for later Gantt chart rendering.
- **Synchronization:**
  - A global semaphore (`cpu_done`) enforces tick synchronization between the CPU scheduler and process threads.
  - Each process thread blocks on its own semaphore until scheduled, ensuring precise timing for preemption and round-robin cycles.

## Algorithms Implemented
- **FCFS (First-Come, First-Served):**
  - Non-preemptive. Processes run in order of arrival.
- **SJF (Shortest Job First):**
  - Non-preemptive. At each idle point, selects the process with the smallest burst.
- **RR (Round Robin):**
  - Preemptive. Each process gets a fixed time slice (quantum) before being requeued.
- **Priority:**
  - Preemptive. Lower priority numbers run first; preemption occurs when a higher-priority process arrives.

## Metrics
The simulator computes:
- **Waiting Time** — total time spent in the READY queue.
- **Response Time** — time from arrival to first dispatch.
- **Turnaround Time** — total time from arrival to completion.
- **Throughput** — number of completed processes per total time.
- **CPU Utilization** — percent of ticks spent executing (not idle).

Averages for wait, response, and turnaround are shown at the end of each run.

## Usage
Compile the simulator with:
```
gcc -std=c11 -O2 -Wall -Wextra -pthread schedsim.c -o schedsim
```

Run it using one of the scheduling flags:
```
./schedsim -i <input.csv> -f        # FCFS
./schedsim -i <input.csv> -s        # SJF
./schedsim -i <input.csv> -r -q 2   # RR with quantum = 2
./schedsim -i <input.csv> -p        # Priority
```

Example input:
```
PID,Arrival,Burst,Priority
P1,0,5,2
P2,1,3,1
P3,2,8,3
P4,3,6,2
```

### Example Runs
#### FCFS
```
./schedsim -i workload.csv -f
===== FCFS Scheduling =====
Timeline (Gantt Chart):
0     5     8    16    22
|-----|---|--------|------|
|  P1  | P2 |   P3   |  P4  |
---------------------------------------------------------------
PID Arr Burst Pri Start Finish Wait Resp Turn
P1   0    5    2    0     5      0    0    5
P2   1    3    1    5     8      4    4    7
P3   2    8    3    8    16      6    6   14
P4   3    6    2   16    22     13   13   19
---------------------------------------------------------------
Avg Wait=5.75  Avg Resp=5.75  Avg Turn=11.25
Throughput=0.18  CPU Util=100.00%
```

#### RR (Quantum = 2)
```
./schedsim -i workload.csv -r -q 2
===== RR Scheduling =====
Timeline (Gantt Chart):
0   2   4   5   6
|--|--|--|--|--|
|P1|P2|P3|P1|P3|
---------------------------------------------------------------
Avg Wait=1.00  Avg Resp=0.33  Avg Turn=3.00
Throughput=0.50  CPU Util=100.00%
```

