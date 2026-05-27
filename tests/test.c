#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define DEVICE_PATH "/dev/my_edu_pci"

int main(int argc, char *argv[]) {
    int fd;
    uint32_t input_val = 5; 
    uint32_t result_val = 0;
    ssize_t bytes_written, bytes_read;

    if (argc > 1) {
        input_val = (uint32_t)strtoul(argv[1], NULL, 10);
    }

    printf("Opening device: %s\n", DEVICE_PATH);
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Error: Could not open device file");
        printf("Hint: Did you load the module and do you have sudo permissions?\n");
        return errno;
    }

    printf("Sending input value: %u\n", input_val);
    bytes_written = write(fd, &input_val, sizeof(input_val));
    if (bytes_written < 0) {
        perror("Error: Failed to write to the device");
        close(fd);
        return errno;
    }

    printf("Waiting for device to calculate (blocking read)...\n");
    bytes_read = read(fd, &result_val, sizeof(result_val));
    if (bytes_read < 0) {
        perror("Error: Failed to read from the device");
        close(fd);
        return errno;
    }

    printf("\n--- Result ---\n");
    printf("Factorial of %u is: %u\n", input_val, result_val);
    printf("--------------\n");

    close(fd);
    return 0;
}
