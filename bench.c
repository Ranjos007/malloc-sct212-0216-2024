#define _DEFAULT_SOURCE
#include "myalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define OPS   50000
#define SLOTS 512
#define SEED  42

static double elapsed_ms(struct timespec a, struct timespec b)
{
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

static void run_custom(const char *label, fit_policy_t policy)
{
    void *ptrs[SLOTS] = {0};
    struct timespec t0, t1;

    set_policy(policy);
    srand(SEED);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < OPS; i++) {
        int idx = rand() % SLOTS;
        if (ptrs[idx]) {
            my_free(ptrs[idx]);
            ptrs[idx] = NULL;
        } else {
            size_t sz = 1 + (size_t)(rand() % 4096);
            ptrs[idx] = my_malloc(sz);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    heap_stats_t s;
    heap_stats(&s);

    double frag = (s.free_bytes > 0)
                      ? 1.0 - ((double)s.largest_free / (double)s.free_bytes)
                      : 0.0;

    printf("| %-10s | %10zu | %10zu | %11zu | %12zu | %8.4f | %8.2f ms |\n",
           label, s.heap_bytes, s.free_bytes, s.free_blocks,
           s.largest_free, frag, elapsed_ms(t0, t1));

    for (int i = 0; i < SLOTS; i++) {
        if (ptrs[i]) my_free(ptrs[i]);
    }
}

static void run_glibc(void)
{
    void *ptrs[SLOTS] = {0};
    struct timespec t0, t1;

    srand(SEED);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < OPS; i++) {
        int idx = rand() % SLOTS;
        if (ptrs[idx]) {
            free(ptrs[idx]);
            ptrs[idx] = NULL;
        } else {
            size_t sz = 1 + (size_t)(rand() % 4096);
            ptrs[idx] = malloc(sz);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    for (int i = 0; i < SLOTS; i++) {
        if (ptrs[i]) free(ptrs[i]);
    }

    printf("| %-10s | %10s | %10s | %11s | %12s | %8s | %8.2f ms |\n",
           "glibc", "N/A", "N/A", "N/A", "N/A", "N/A", elapsed_ms(t0, t1));
}

int main(void)
{
    printf("| Policy     | heap_bytes | free_bytes | free_blocks | largest_free | Ext Frag | Time       |\n");
    printf("|------------|------------|------------|-------------|--------------|----------|------------|\n");
    run_custom("FIT_FIRST", FIT_FIRST);
    run_custom("FIT_BEST", FIT_BEST);
    run_glibc();
    return 0;
}
