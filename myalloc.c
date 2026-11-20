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

static void coalesce(void)
{
    block_t *cur = head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free && is_adjacent(cur, cur->next)) {
            cur->size += HDR + cur->next->size;
            if (cur->next == tail) tail = cur;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

void *my_malloc(size_t size)
{
    if (size == 0) return NULL;
    size_t need = align_up(size);
    block_t *b  = find_fit(need);
    if (!b) {
        b = grow(need);
        if (!b) return NULL;
        coalesce();
        b = find_fit(need);
        if (!b) return NULL;
    }
    split_block(b, need);
    b->free = 0;
    return payload(b);
}

void my_free(void *ptr)
{
    if (!ptr) return;
    if (((uintptr_t)ptr % 16u) != 0) {
        write_str(STDERR_FILENO, "my_free: invalid pointer\n");
        return;
    }
    block_t *b = header(ptr);
    if (b->magic != MAGIC) {
        write_str(STDERR_FILENO, "my_free: invalid pointer\n");
        return;
    }
    if (b->free) {
        write_str(STDERR_FILENO, "my_free: double free detected\n");
        return;
    }
    b->free = 1;
    coalesce();
}

void *my_calloc(size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0) return NULL;
    if (size > SIZE_MAX / nmemb) return NULL;
    size_t total = nmemb * size;
    void *ptr    = my_malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *my_realloc(void *ptr, size_t size)
{
    if (!ptr) return my_malloc(size);
    if (size == 0) { my_free(ptr); return NULL; }

    block_t *b = header(ptr);
    if (b->magic != MAGIC) return NULL;

    size_t need = align_up(size);
    if (b->size >= need) {
        split_block(b, need);
        coalesce();
        return ptr;
    }
    if (b->next && b->next->free && is_adjacent(b, b->next) &&
        (b->size + HDR + b->next->size >= need)) {
        b->size += HDR + b->next->size;
        if (b->next == tail) tail = b;
        b->next = b->next->next;
        split_block(b, need);
        coalesce();
        return ptr;
    }
    void *new_ptr = my_malloc(size);
    if (!new_ptr) return NULL;
    size_t copy_len = b->size < size ? b->size : size;
    memcpy(new_ptr, ptr, copy_len);
    my_free(ptr);
    return new_ptr;
}

void set_policy(fit_policy_t p)
{
    current_policy = p;
}

void heap_stats(heap_stats_t *out)
{
    if (!out) return;
    out->heap_bytes   = total_heap_bytes;
    out->used_bytes   = 0;
    out->free_bytes   = 0;
    out->blocks       = 0;
    out->free_blocks  = 0;
    out->largest_free = 0;

    for (block_t *cur = head; cur != NULL; cur = cur->next) {
        out->blocks++;
        if (cur->free) {
            out->free_bytes += cur->size;
            out->free_blocks++;
            if (cur->size > out->largest_free) out->largest_free = cur->size;
        } else {
            out->used_bytes += cur->size;
        }
    }
}

void heap_dump(void)
{
    char buf[128];
    size_t idx = 0;
    for (block_t *cur = head; cur != NULL; cur = cur->next, idx++) {
        int len = snprintf(buf, sizeof(buf), "[%2zu] %p size=%zu %s\n",
                           idx, payload(cur), cur->size,
                           cur->free ? "FREE" : "USED");
        if (len > 0) {
            size_t n = (size_t)len < sizeof(buf) ? (size_t)len : sizeof(buf) - 1;
            ssize_t r = write(STDOUT_FILENO, buf, n);
            (void)r;
        }
    }
}

int heap_check(void)
{
    for (block_t *cur = head; cur != NULL; cur = cur->next) {
        if (cur->magic != MAGIC) return 0;
        if (((uintptr_t)payload(cur) % 16u) != 0) return 0;
        if (cur->next) {
            if ((uintptr_t)cur >= (uintptr_t)cur->next) return 0;
            if ((char *)cur + HDR + cur->size > (char *)cur->next) return 0;
            if (cur->free && cur->next->free && is_adjacent(cur, cur->next)) return 0;
        }
    }
    return 1;
}
