
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define FILE_SIZE (1024LL * 1024 * 1024)
#define TEST_FILE "benchmark_test.dat"

size_t buffer_sizes[] = {
    4096,
    8192,
    16384,

    32768,
    65536,
    131072,
    262144,
    524288,
    1048576,
    2097152,
    4194304,
    8388608,
    16777216
};

#define NUM_BUFFERS (sizeof(buffer_sizes) / sizeof(buffer_sizes[0]))

double get_time(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int write_test(size_t buffer_size)
{
    FILE *file;
    char *buffer;
    long long remaining;
    double start, end;

    buffer = malloc(buffer_size);

    if (buffer == NULL)
    {
        perror("malloc");
        return -1;
    }

    memset(buffer, 'A', buffer_size);

    file = fopen(TEST_FILE, "wb");

    if (file == NULL)
    {
        perror("fopen");
        free(buffer);
        return -1;
    }

    if (setvbuf(file, NULL, _IOFBF, buffer_size) != 0)
    {
        fprintf(stderr, "Warning: setvbuf failed for %zu bytes\n",
                buffer_size);
    }

    remaining = FILE_SIZE;

    start = get_time();

    while (remaining > 0)
    {
        size_t to_write =
            remaining < (long long)buffer_size
                ? (size_t)remaining
                : buffer_size;

        if (fwrite(buffer, 1, to_write, file) != to_write)
        {
            perror("fwrite");
            fclose(file);
            free(buffer);
            return -1;
        }

        remaining -= to_write;
    }

    fflush(file);

    end = get_time();

    fclose(file);
    free(buffer);

    return (int)((end - start) * 1000);
}

int read_test(size_t buffer_size)
{
    FILE *file;
    char *buffer;
    double start, end;

    buffer = malloc(buffer_size);

    if (buffer == NULL)
    {
        perror("malloc");
        return -1;
    }

    file = fopen(TEST_FILE, "rb");

    if (file == NULL)
    {
        perror("fopen");
        free(buffer);
        return -1;
    }

    if (setvbuf(file, NULL, _IOFBF, buffer_size) != 0)
    {
        fprintf(stderr, "Warning: setvbuf failed for %zu bytes\n",
                buffer_size);
    }

    start = get_time();

    while (fread(buffer, 1, buffer_size, file) > 0)
    {
        /* Read entire file */
    }

    end = get_time();

    fclose(file);
    free(buffer);

    return (int)((end - start) * 1000);
}

int main(void)
{
    printf("pgmoneta I/O Buffer Size Benchmark\n");
    printf("File size: 1 GB\n\n");

    printf("%-12s %-15s %-15s\n",
           "Buffer Size", "Write Time(ms)", "Read Time(ms)");

    printf("--------------------------------------------------\n");

    for (size_t i = 0; i < NUM_BUFFERS; i++)
    {
        size_t buffer_size = buffer_sizes[i];

        int write_time = write_test(buffer_size);

        if (write_time < 0)
        {
            fprintf(stderr, "Write test failed.\n");
            return EXIT_FAILURE;
        }

        int read_time = read_test(buffer_size);

        if (read_time < 0)
        {
            fprintf(stderr, "Read test failed.\n");
            return EXIT_FAILURE;
        }

        printf("%-12zu %-15d %-15d\n",
               buffer_size,
               write_time,
               read_time);

        remove(TEST_FILE);
    }

    printf("\nBenchmark completed successfully.\n");

    return EXIT_SUCCESS;
}
