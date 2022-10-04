#include "string"
#include "track_access.h"

int main(int argc, char *argv[])
{
    int cycles = stoi(argv[0]);
    init();

    for (int i = 0; i < cycles; i++)
    {
        // generate random 64 bit int (vaddr)
        uint64_t x = ((((((rand() % 65535) << 16) | (rand() % 65535)) << 16) | (rand() % 65535)) << 16) | (rand() % 65535);

        // track vaddr access
        track_access(x);
    }

    printf("memory accesses = %d\n", total_num_accesses);
    printf("cache hits = %d\n", num_hits);
    printf("cache misses = %d\n", num_misses);
}