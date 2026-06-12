#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sched.h>
#include <fcntl.h>

/* buffer guard */
#ifndef BUFFER_SIZE
#define BUFFER_SIZE 10
#endif

/* external checksum(), provided by prof in Checksum.c */
extern uint16_t checksum(char *addr, uint32_t count);

/* the buffer & synchronization primitives */
BUFFER_ITEM buffer[BUFFER_SIZE];
int in = 0;   // index of next item to produce
int out = 0;  // index of next item to consume

/* semaphores and mutex */
sem_t *empty = NULL; // semaphore counting empty buffer slots
sem_t *full = NULL;  // semaphore counting full buffer slots
pthread_mutex_t mutex; // mutex for critical section
int produced_total = 0;
int consumed_total = 0;

/*----------------------------------------------------------------------------------*/

/* insert an object into buffer
return 0 if successful, otherwise
return -1 indicating an error condition */
int insert_item(BUFFER_ITEM item) {
    // acquire empty semaphore
    if (sem_wait(empty) != 0) {
        perror("sem_wait (empty)");
        return -1;
    }

    // acquire mutex lock to protect buffer
     if (pthread_mutex_lock(&mutex) != 0) {
        perror("pthread_mutex_lock (insert_item)");
        sem_post(empty); // release empty semaphore on error
        return -1;
    }

    // copy local item to global buffer at index 'in', then update 'in' index
    buffer[in] = item;
    in = (in + 1) % BUFFER_SIZE;

    // release mutex and post full semaphore
    pthread_mutex_unlock(&mutex);
    sem_post(full);

    return 0;
}

/*----------------------------------------------------------------------------------*/

/* remove an object from buffer
placing it in item
return 0 if successful, otherwise
return -1 indicating an error condition */
int remove_item(BUFFER_ITEM *item) {
    // acquire full semaphore
    if (sem_wait(full) != 0) {
        perror("sem_wait (full)");
        return -1;
    }

    // acquire mutex lock to protect buffer
    if (pthread_mutex_lock(&mutex) != 0) {
        perror("pthread_mutex_lock (remove_item)");
        sem_post(full); // release full semaphore on error
        return -1;
    }

    // copy item from global buffer at index 'out' to local item, then update 'out' index
    *item = buffer[out];
    out = (out + 1) % BUFFER_SIZE;

    // release mutex and post empty semaphore
    pthread_mutex_unlock(&mutex);
    sem_post(empty);

    return 0;
}

/*----------------------------------------------------------------------------------*/

/* 40% chance to voluntarily yield CPU (simulate preemption) */
static inline void rand_yield() {
    if ((rand() % 10) < 4) {
        sched_yield();
    }
}

/*----------------------------------------------------------------------------------*/

/**
 * producer thread
 * generates random data and computes checksum before inserting into buffer
 */
void *producer(void *param) {
    (void)param;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    BUFFER_ITEM item;

    while (1) {
        rand_yield();

        // produce an item
        for (int i = 0; i < 30; i++) {
            item.data[i] = (uint8_t)(rand() % 256);
        }
        item.cksum = checksum((char *)item.data, (uint32_t)30);
    
        if (insert_item(item) != 0) {
            fprintf(stderr, "Producer: Error inserting item into buffer\n");
        } else {
            pthread_mutex_lock(&mutex);
            produced_total++;
            
            printf("Producer [%llu]: produced item #%d (checksum %04X)\n",
                   (unsigned long long)pthread_self(), produced_total, item.cksum);
            pthread_mutex_unlock(&mutex);
        }

        pthread_testcancel();
    }

    return NULL;
}

/**
 * consumer thread
 * removes item from buffer and verifies checksum
 */
void *consumer(void *param) {
    (void)param;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    BUFFER_ITEM item;

    while (1) {
        rand_yield();

        if (remove_item(&item) != 0) {
            fprintf(stderr, "Consumer: Error removing item from buffer\n");
            pthread_testcancel();
            continue;
        } else {
            pthread_mutex_lock(&mutex);
            consumed_total++;
            
            printf("Consumer [%llu]: consumed item #%d (expected checksum %04X)\n",
                   (unsigned long long)pthread_self(), consumed_total, item.cksum);
            pthread_mutex_unlock(&mutex);
        }

        // verify checksum
        uint16_t cksum = checksum((char *)item.data, (uint32_t)30);
        if (cksum != item.cksum) {
            fprintf(stderr, "Consumer: Checksum mismatch! Expected %04X, got %04X\n", item.cksum, cksum);
            _exit(EXIT_FAILURE); // per spec: exit program on mismatch
        } else {
            // printf("Consumer: Checksum verified: %04X\n", cksum);
        }

        pthread_testcancel();
    }

    return NULL;
}

/*----------------------------------------------------------------------------------*/

/* parse_args: parse and validate command line args */
int parse_args(int argc, char *argv[], int *delay, int *num_producers, int *num_consumers) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <delay> <# producers> <# consumers>\n", argv[0]);
        return -1;
    }
    
    char *endptr = NULL;
    long val = strtol(argv[1], &endptr, 10);
    if (*argv[1] == '\0' || *endptr != '\0' || val < 0 || val > INT32_MAX) {
        fprintf(stderr, "Invalid delay value: %s\n", argv[1]);
        return -1;
    }
    *delay = (int)val;

    endptr = NULL;
    val = strtol(argv[2], &endptr, 10);
    if (*argv[2] == '\0' || *endptr != '\0' || val <= 0 || val > INT32_MAX) {
        fprintf(stderr, "Invalid number of producers: %s\n", argv[2]);
        return -1;
    }
    *num_producers = (int)val;

    endptr = NULL;
    val = strtol(argv[3], &endptr, 10);
    if (*argv[3] == '\0' || *endptr != '\0' || val <= 0 || val > INT32_MAX) {
        fprintf(stderr, "Invalid number of consumers: %s\n", argv[3]);
        return -1;
    }
    *num_consumers = (int)val;

    return 0;
}

/*----------------------------------------------------------------------------------*/

/* 1. get command line args argv[1], argv[2], argv[3] */
    /* 2. initialize buffer */
    /* 3. create producer thread(s) */
    /* 4. create consumer thread(s) */
    /* 5. sleep */
    /* 6. exit */
int main(int argc, char *argv[]) {
    int delay, num_producers, num_consumers;
    if (parse_args(argc, argv, &delay, &num_producers, &num_consumers) != 0) {
        return EXIT_FAILURE;
    }

    srand((unsigned int)time(NULL)); // seed random number generator

    pthread_mutex_init(&mutex, NULL);

    sem_unlink("/empty_sem");
    sem_unlink("/full_sem");

    // initialize semaphores
    empty = sem_open("/empty_sem", O_CREAT | O_EXCL, 0644, BUFFER_SIZE);
    if (empty == SEM_FAILED) {
        perror("sem_open empty");
        return EXIT_FAILURE;
    }

    full = sem_open("/full_sem", O_CREAT | O_EXCL, 0644, 0);
    if (full == SEM_FAILED) {
        perror("sem_open full");
        sem_unlink("/empty_sem");
        return EXIT_FAILURE;
    }

    pthread_t *producers = malloc(num_producers * sizeof(pthread_t));
    pthread_t *consumers = malloc(num_consumers * sizeof(pthread_t));
    if (producers == NULL || consumers == NULL) {
        perror("Failed to allocate memory for threads");
        return EXIT_FAILURE;
    }

    // create producer threads
    for (int i = 0; i < num_producers; i++) {
        pthread_create(&producers[i], NULL, producer, NULL);
    }

    // create consumer threads
    for (int i = 0; i < num_consumers; i++) {
        pthread_create(&consumers[i], NULL, consumer, NULL);
    }

    sleep(delay); // main thread sleeps for specified delay

    // cancel all threads
    for (int i = 0; i < num_producers; i++) pthread_cancel(producers[i]);
    for (int i = 0; i < num_consumers; i++) pthread_cancel(consumers[i]);

    // join all threads
    for (int i = 0; i < num_producers; i++) pthread_join(producers[i], NULL);
    for (int i = 0; i < num_consumers; i++) pthread_join(consumers[i], NULL);

    // clean up
    free(producers);
    free(consumers);
    pthread_mutex_destroy(&mutex);

    sem_close(empty);
    sem_close(full);
    sem_unlink("/empty_sem");
    sem_unlink("/full_sem");

    printf("\n--- Program complete ---\n");
    printf("Total produced: %d\n", produced_total);
    printf("Total consumed: %d\n", consumed_total);

    return EXIT_SUCCESS;
}