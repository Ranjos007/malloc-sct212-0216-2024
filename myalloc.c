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

static block_t *find_fit(size_t need)
{
    block_t *best = NULL;
    for (block_t *cur = head; cur != NULL; cur = cur->next) {
        if (cur->free && cur->size >= need) {
            if (current_policy == FIT_FIRST) return cur;
            if (!best || cur->size < best->size) best = cur;
        }
    }
    return best;
}

static void split_block(block_t *b, size_t need)
{
    if (b->size < need + HDR + MIN_PAYLOAD) return;

    block_t *rem = (block_t *)((char *)b + HDR + need);
    rem->size    = b->size - need - HDR;
    rem->free    = 1;
    rem->magic   = MAGIC;
    rem->next    = b->next;

    b->size = need;
    b->next = rem;
    if (tail == b) tail = rem;
}
