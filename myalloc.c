#define _DEFAULT_SOURCE
#include "myalloc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAGIC       0x5A110C42
#define MIN_CHUNK   (64u * 1024u)
#define MIN_PAYLOAD 16u

typedef struct block {
    size_t        size;   /* payload size in bytes             */
    struct block *next;   /* next block, in increasing address */
    int           free;   /* 1 = free, 0 = in use              */
    int           magic;  /* constant to detect bad pointers   */
} block_t;

#define HDR ((sizeof(block_t) + 15u) & ~(size_t)15u)

static block_t     *head             = NULL;
static block_t     *tail             = NULL;
static size_t       total_heap_bytes = 0;
static fit_policy_t current_policy   = FIT_FIRST;

static size_t align_up(size_t n) { return (n + 15u) & ~(size_t)15u; }
static void *payload(block_t *b) { return (char *)b + HDR; }
static block_t *header(void *ptr) { return (block_t *)((char *)ptr - HDR); }
static int is_adjacent(block_t *a, block_t *b) { return ((char *)a + HDR + a->size) == (char *)b; }
static void write_str(int fd, const char *s) { ssize_t r = write(fd, s, strlen(s)); (void)r; }

static block_t *grow(size_t need)
{
    size_t chunk = HDR + need;
    if (chunk < MIN_CHUNK) chunk = MIN_CHUNK;

    void *cur_brk = sbrk(0);
    if (cur_brk == (void *)-1) return NULL;

    uintptr_t addr = (uintptr_t)cur_brk;
    size_t pad = (16u - (addr % 16u)) % 16u;
    size_t req = pad + chunk;

    void *raw = sbrk((intptr_t)req);
    if (raw == (void *)-1) return NULL;

    total_heap_bytes += req;

    block_t *b = (block_t *)((char *)raw + pad);
    b->size    = chunk - HDR;
    b->next    = NULL;
    b->free    = 1;
    b->magic   = MAGIC;

    if (!head) head = tail = b;
    else { tail->next = b; tail = b; }
    return b;
}
