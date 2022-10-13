#include "string"
#include "track_access.h"
// test

int main(int argc, char *argv[])
{
    int cycles = stoi(argv[1]);
    init_cache();

    // generate random 64 bit int (vaddr)
    uint64_t x = ((((((rand() % 65535) << 16) | (rand() % 65535)) << 16) | (rand() % 65535)) << 16) | (rand() % 65535);

    for (int i = 0; i < cycles; i++)
    {
        // track vaddr access
        track_access(x);
    }

    printf("memory accesses = %lu\n", total_num_accesses);
    printf("cache hits = %lu\n", num_hits);
    printf("cache misses = %lu\n", num_misses);
}