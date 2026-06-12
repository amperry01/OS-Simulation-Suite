# OS Simulation Suite

A collection of C/C++ operating systems simulations exploring thread synchronization, CPU scheduling, and memory management.

These projects implement core operating system concepts including concurrent execution, process scheduling algorithms, and dynamic memory allocation strategies.

## Projects

### Thread Synchronization
Producer-consumer simulation using POSIX threads, mutexes, and semaphores. Multiple producer and consumer threads coordinate access to a shared bounded buffer while maintaining thread-safe synchronization and checksum validation.

**Concepts:** Concurrency, synchronization primitives, mutexes, semaphores, multithreading

### CPU Scheduling
CPU scheduler simulator implementing FCFS, SJF, Round Robin, and Priority scheduling algorithms. Simulates process execution and reports performance metrics such as waiting time, turnaround time, response time, throughput, and CPU utilization.

**Concepts:** Scheduling algorithms, process management, performance analysis

### Memory Management
Contiguous memory allocation simulator supporting First-Fit, Best-Fit, and Worst-Fit allocation strategies. Includes memory compaction, fragmentation tracking, and allocation visualization.

**Concepts:** Dynamic memory allocation, fragmentation, compaction, operating system memory management

## Tech Stack

- C / C++
- POSIX Threads (pthreads)
- Mutexes & Semaphores
- Systems Programming
- Process Scheduling
- Memory Allocation Algorithms

## Why This Project?

Operating systems are the foundation of modern software systems. These projects were built to explore how operating systems coordinate concurrent execution, schedule processes, and manage memory resources at a low level.

Each project focuses on a different subsystem commonly found in real operating systems and provides a hands-on implementation of the underlying algorithms and data structures.
