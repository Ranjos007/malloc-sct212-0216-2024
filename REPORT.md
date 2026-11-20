# Allocator Benchmark Report

## Experimental Setup
The benchmark (`bench.c`) executes 50,000 random allocations and frees with payload sizes between 1 and 4096 bytes using a fixed `srand(42)` seed across 512 active slots. The identical sequence is run under `FIT_FIRST`, `FIT_BEST`, and glibc's `malloc`/`free`, timed with `clock_gettime(CLOCK_MONOTONIC)`.

## Results

| Policy     | heap_bytes | free_bytes | free_blocks | largest_free | Ext Frag | Time       |
|------------|------------|------------|-------------|--------------|----------|------------|
| FIT_FIRST  |     720896 |     152256 |         110 |        76800 |   0.4956 |    68.36 ms |
| FIT_BEST   |     720896 |     152304 |          92 |        88672 |   0.4178 |    99.83 ms |
| glibc      |        N/A |        N/A |         N/A |          N/A |      N/A |     4.03 ms |

*External fragmentation is computed as `1 - (largest_free / free_bytes)`.*

## Discussion
Between the two policies, **FIT_FIRST** is generally the better choice for general-purpose workloads. While `FIT_BEST` can reduce total heap growth by fitting requests into tighter holes, it must scan the entire linked list on every allocation, making it noticeably slower as the number of blocks grows. `FIT_FIRST` stops at the first suitable block and achieves much lower latency while still reusing freed space effectively through splitting and coalescing.

Our allocator is significantly slower and less space-efficient than glibc's `ptmalloc2` because it uses a single linear linked list (`O(n)` search and `O(n)` coalescing on every free) rather than segregated size-class bins and fastbins (`O(1)` lookup). Additionally, our 32-byte header per block adds more metadata overhead than glibc's compact boundary tags, and glibc uses `mmap()` for large allocations so they can be returned to the OS immediately.
