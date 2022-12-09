#include "cache.h"

unsigned long total_num_accesses = 0, num_misses = 0, num_hits = 0, num_evicts = 0;
unsigned long node_array_evicts = 0, edge_array_evicts = 0, prop_array_evicts = 0, in_wl_evicts = 0, out_wl_evicts = 0;
unsigned long node_array_accesses = 0, edge_array_accesses = 0, prop_array_accesses = 0, in_wl_accesses = 0, out_wl_accesses = 0;
uint64_t node_start, node_end, edge_start, edge_end, prop_start, prop_end, in_wl_start, in_wl_end, out_wl_start, out_wl_end;

// 1 GB = 1073741824 bytes
// 0.5 GB = 536870912 bytes
// 0.25 GB = 268435456 bytes
// 0.125 GB = 134217728 bytes
// 0.0625 GB = 67108864 bytes
// 1/512 GB = 0.001953125 GB = 2097152 bytes
#define RAM_SIZE 1073741824 // DEFINE RAM SIZE (IN BYTES) HERE
#define EVICTION_POLICY LRU   // SET EVICTION POLICY HERE

FunctionalCache *ram;
bool dirty_evict;
int64_t evicted_tag;
uint64_t evicted_offset;
int limit = 10;

void init_cache()
{
    dirty_evict = false;
    evicted_tag = -1;
    evicted_offset = 0;

    ram = new FunctionalCache(RAM_SIZE, RAM_SIZE / PAGE_SIZE, PAGE_SIZE, EVICTION_POLICY);
}

void init_cache(Replacement_Policy eval_pol, Promotion_Policy promo_pol, User_Aware aware, int hugepage_lim, int tau_promotion)
{
    dirty_evict = false;
    evicted_tag = -1;
    evicted_offset = 0;

    ram = new FunctionalCache(RAM_SIZE, RAM_SIZE / PAGE_SIZE, PAGE_SIZE, eval_pol, promo_pol, aware, hugepage_lim, tau_promotion);
    // cout << "Running with huge pages for return (prop) array ONLY -- all else is 4kb page\n";
    // cout << "Running with CUSTOM promotion policy\n";
    cout << "User is " << (aware == AWARE ? "aware" : "unaware") << " of huge paging" << endl;
    cout << "RAM size (in bytes): " << RAM_SIZE << endl;
    cout << "Hugepage Limit = " << hugepage_lim << endl;
}

void init_cache_manual(unsigned int ram_size, unsigned int page_size, Replacement_Policy eviction_policy)
{
    dirty_evict = false;
    evicted_tag = -1;
    evicted_offset = 0;

    ram = new FunctionalCache(ram_size, ram_size / page_size, page_size, eviction_policy);
}

void count(uint64_t evicted, bool evicted_bool)
{
    if (evicted_bool)
    {
        if (evicted >= node_start && evicted <= node_end)
            node_array_evicts++;
        else if (evicted >= edge_start && evicted <= edge_end)
            edge_array_evicts++;
        else if (evicted >= prop_start && evicted <= prop_end)
            prop_array_evicts++;
        else if (evicted >= in_wl_start && evicted <= in_wl_end)
            in_wl_evicts++;
        else if (evicted >= out_wl_start && evicted <= out_wl_end)
            out_wl_evicts++;
    }
    else
    {
        if (evicted >= node_start && evicted <= node_end)
            node_array_accesses++;
        else if (evicted >= edge_start && evicted <= edge_end)
            edge_array_accesses++;
        else if (evicted >= prop_start && evicted <= prop_end)
            prop_array_accesses++;
        else if (evicted >= in_wl_start && evicted <= in_wl_end)
            in_wl_accesses++;
        else if (evicted >= out_wl_start && evicted <= out_wl_end)
            out_wl_accesses++;
    }
}

void print_pages_end()
{
    ram->print_page_counts();
}

void track_access(uint64_t vaddr, bool is2mb=false)
{
    uint64_t page, evicted_addr;
    bool hit;
    vector<uint64_t> evicted;

    total_num_accesses++;

    page = (uint64_t)(vaddr / PAGE_SIZE); // virtual page number

    // get physical page?

    /********** START: cache logic **********/
    hit = ram->access(page * PAGE_SIZE, true, is2mb);
    // if (!hit && is2mb)
    // {
    //     cout << "Expected huge page and missed\n";
    // }
    count(page * PAGE_SIZE, false);
    if (!hit)
    { // miss
        ram->insert(page * PAGE_SIZE, true, &dirty_evict, &evicted_tag, &evicted_offset, is2mb);
        if (evicted_tag != -1)
        {
            // evicted_addr = evicted_tag * PAGE_SIZE + evicted_offset / PAGE_SIZE;
            evicted_addr = evicted_tag * PAGE_SIZE + evicted_offset;
            // if (limit > 0)
            // {
            //     cout << "evicted addr = " << evicted_addr << "\n";
            //     limit--;
            // }
            count(evicted_addr, true);
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