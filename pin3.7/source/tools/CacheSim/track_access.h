#include "cache.h"

unsigned long total_num_accesses = 0, num_misses = 0, num_hits = 0;

#define RAM_SIZE 1073741824 // DEFINE RAM SIZE (IN BYTES) HERE
#define EVICTION_POLICY 0   // SET EVICTION POLICY HERE

FunctionalCache *ram;
bool dirty_evict;
int64_t evicted_tag;
uint64_t evicted_offset;

void init_cache()
{
    dirty_evict = false;
    evicted_tag = -1;
    evicted_offset = 0;

    ram = new FunctionalCache(RAM_SIZE, RAM_SIZE / PAGE_SIZE, PAGE_SIZE, EVICTION_POLICY);
}

void track_access(uint64_t vaddr)
{
    uint64_t page, evicted_addr;
    bool hit;
    vector<uint64_t> evicted;

    total_num_accesses++;

    page = (uint64_t)(vaddr / PAGE_SIZE); // virtual page number

    // get physical page?

    /********** START: cache logic **********/
    hit = ram->access(page * PAGE_SIZE, true);
    if (!hit)
    { // miss
        ram->insert(page * PAGE_SIZE, true, &dirty_evict, &evicted_tag, &evicted_offset);
        if (evicted_tag != -1)
        {
            evicted_addr = evicted_tag * PAGE_SIZE + evicted_offset / PAGE_SIZE;
            evicted.push_back(evicted_addr); // track evicted address?
        }
        num_misses++;
    }
    else
    { // hits
        num_hits++;
    }
    /********** END: promotion cache logic **********/
}
