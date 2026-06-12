# PA2: Thread Synchronization

This program demonstrates the producer–consumer problem using POSIX threads, mutexes, and semaphores. 
Multiple producer and consumer threads share a bounded buffer where each producer generates random data items, 
computes a checksum, and places the items into the buffer. Consumers remove the items, verify their checksums, 
and report mismatches (if any). The program runs for a user-defined time interval before gracefully canceling 
all threads and printing production/consumption totals.

## Features
- **Bounded Buffer:** Implements a circular buffer of fixed size (`BUFFER_SIZE = 10`)
- **Producer Threads:** Generate 30 random data bytes and a checksum for each item
- **Consumer Threads:** Remove items from the buffer and verify checksum integrity
- **Synchronization:** Uses three synchronization primitives:
  - `mutex` — a mutual exclusion lock for buffer access
  - `empty` — counting semaphore tracking available slots
  - `full` — counting semaphore tracking filled slots
- **Thread Management:** Supports user-specified numbers of producer and consumer threads
- **Graceful Termination:** Cancels threads and joins them after a delay specified by the user

## Implementation Details
- **System Calls / APIs:** `pthread_create()`, `pthread_cancel()`, `pthread_join()`, `sem_open()`, `sem_wait()`, `sem_post()`, `sem_close()`, `sem_unlink()`, and `sched_yield()`
- **Shared Buffer:** Defined in `buffer.h` as:
  ```c
  typedef struct buffer_item {
      uint8_t data[30];
      uint16_t cksum;
  } BUFFER_ITEM;

  #define BUFFER_SIZE 10
  ```

## Usage
Compile with:
```
gcc prodcon.c -lpthread -o prodcon
```
Run with:
```
./prodcon <delay> <num_producers> <num_consumers>
```

## Example Output

```
$ ./prodcon 5 2 2
Producer [4335325184]: produced item #1 (checksum 3F4A)
Producer [4335325184]: produced item #2 (checksum 1C82)
Consumer [4335312384]: consumed item #1 (expected checksum 3F4A)
Consumer [4335312384]: consumed item #2 (expected checksum 1C82)
...
--- Program complete ---
Total produced: 441142
Total consumed: 477176
```

*Note:* Exact totals vary each run depending on system scheduling and thread timing. Small differences are expected because threads may still be active during cancellation.
