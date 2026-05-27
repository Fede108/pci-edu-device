#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#define DEVICE_PATH "/dev/my_edu_pci"
#define NUM_WRITERS 2
#define NUM_READERS 2
#define ITERATIONS 4

int fd;

typedef struct {
    int thread_id;
} thread_info_t;

void *writer_worker(void *arg) {
    thread_info_t *info = (thread_info_t *)arg;
    uint32_t input_val = 3 + info->thread_id; 

    for (int i = 0; i < ITERATIONS; i++) {
        printf("[Writer %d] Writing value: %u (Iteration %d)\n", info->thread_id, input_val, i + 1);
        
        ssize_t bytes_written = write(fd, &input_val, sizeof(input_val));
        if (bytes_written < 0) {
            fprintf(stderr, "[Writer %d] Write failed: %s\n", info->thread_id, strerror(errno));
        }

        usleep(rand() % 50000); 
    }
    return NULL;
}

void *reader_worker(void *arg) {
    thread_info_t *info = (thread_info_t *)arg;
    uint32_t result_val = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        
        ssize_t bytes_read = read(fd, &result_val, sizeof(result_val));
        if (bytes_read < 0) {
            fprintf(stderr, "[Reader %d] Read failed: %s\n", info->thread_id, strerror(errno));
        } else {
            printf("[Reader %d] Caught Result: %u\n", info->thread_id, result_val);
        }
        
        usleep(rand() % 30000);
    }
    return NULL;
}

int main() {
    pthread_t writers[NUM_WRITERS];
    pthread_t readers[NUM_READERS];
    thread_info_t writer_info[NUM_WRITERS];
    thread_info_t reader_info[NUM_READERS];

    srand(time(NULL));

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return errno;
    }

    for (int i = 0; i < NUM_READERS; i++) {
        reader_info[i].thread_id = i;
        pthread_create(&readers[i], NULL, reader_worker, &reader_info[i]);
    }

    for (int i = 0; i < NUM_WRITERS; i++) {
        writer_info[i].thread_id = i;
        pthread_create(&writers[i], NULL, writer_worker, &writer_info[i]);
    }

    for (int i = 0; i < NUM_WRITERS; i++) {
        pthread_join(writers[i], NULL);
    }
    for (int i = 0; i < NUM_READERS; i++) {
        pthread_join(readers[i], NULL);
    }

    close(fd);
    printf("\nStress test complete.\n");
    return 0;
}
