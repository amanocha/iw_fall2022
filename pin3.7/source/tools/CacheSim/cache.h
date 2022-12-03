#include <unordered_map>
#include <map>
#include <vector>
#include <set>
#include <bitset>
#include <math.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#define L1_TLB_4K_ENTRIES 64
#define L1_TLB_2M_ENTRIES 32
#define L2_TLB_ENTRIES 1536
#define PAGE_SIZE 4096
#define SUPERPAGE_SIZE 2097152
#define L1_ASSOC 4
#define L2_ASSOC 6
#define MAX_FREQ 255

// Bounds for CUSTOM promotion policy
#define TAU_PROMOTION 250
#define HUGEPAGE_LIMIT 100

#define TAU_WSCLOCK 5

using namespace std;


enum Replacement_Policy 
{
  LRU,
  LRU_HALF,
  LFU,
  CLOCK,
  FIFO,
  WS_CLOCK
};

enum Promotion_Policy
{
  NA,
  HAWKEYE,
  INGENS,
  CUSTOM
};

struct CacheLine
{
  uint64_t addr;
  uint64_t offset;
  CacheLine *prev;
  CacheLine *next;
  bool dirty = false;
  bool isHugePage = false;

  unsigned short freq = 0;
  unsigned short timer = 0;
};

class CacheSet
{
public:
  CacheLine *head;
  CacheLine *tail;
  CacheLine *clockPointer;
  bool printed_once = false;
  CacheLine *entries;
  std::vector<CacheLine *> freeEntries;
  std::unordered_map<uint64_t, CacheLine *> addr_map;
  std::unordered_map<uint64_t, int> acvs;
  std::set<uint64_t> hugepages;
  int access_cycles = TAU_PROMOTION;

  unsigned int associativity;
  int line_size;
  unsigned policy;

  CacheSet(int size, int cache_line_size, Replacement_Policy eviction_policy, Promotion_Policy promotion_policy=NA)
  {
    associativity = size;
    line_size = cache_line_size;
    policy = eviction_policy;
    cout << "policy = " << policy << "\n";

    CacheLine *c;
    entries = new CacheLine[associativity];
    for (unsigned int i = 0; i < associativity; i++)
    {
      c = &entries[i];
      freeEntries.push_back(c);
    }
    if (policy == CLOCK || policy == WS_CLOCK)
    {
      // set up circular linked list for Clock algo
      head = new CacheLine;
      tail = new CacheLine;
      head->prev = tail;
      head->next = tail;
      tail->prev = head;
      tail->next = head;
      clockPointer = head;
    }
    else
    {
      head = new CacheLine;
      tail = new CacheLine;
      head->prev = NULL;
      head->next = tail;
      tail->next = NULL;
      tail->prev = head;
    }
  }
  ~CacheSet()
  {
    delete head;
    delete tail;
    delete[] entries;
  }

  // access() is called only when we have a page hit
  // this is to reorder the nodes (for LFU, LRU, etc.)
  // NOT meant to evict or delete any pages
  bool access(uint64_t address, uint64_t offset, bool isLoad)
  {
    CacheLine *c = addr_map[address];
    if (c)
    { // Hit

      // policy 3 == Clock Algo, 4 == FIFO
      // only want to delete if policy is not Clock Algo or FIFO
      if (policy == LRU || policy == LRU_HALF || policy == LFU)
        deleteNode(c);

      if (policy == LRU)
      {
        insertFront(c, head);
      }
      else if (policy == LRU_HALF)
      {
        if (c->freq >= associativity / 2)
          insertFront(c, head);
      }
      else if (policy == LFU)
      {
        insertLFU(c, head);
      }
      else if (policy == WS_CLOCK)
      {
        tick_WSClock();
        c->timer = 0;
      }

      // TODO
      // add ACV updating here for each promotion policy
      
      // also add periodic "upgrades" every n accesses to promote/demote pages using the policy
      // this effectively behaves as the daemon thread for promotions

      if (!isLoad)
        c->dirty = true;

      c->offset = offset;
      c->freq++;
      if (c->freq == MAX_FREQ)
        decay_freqs();

      return true;
    }
    else
    {
      return false;
    }
  }

  void insert(uint64_t address, uint64_t offset, bool isLoad, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset, bool is2M=false)
  {
    CacheLine *c = addr_map[address];
    bool eviction = is2M ? (freeEntries.size() < 512) : (freeEntries.size() == 0);

    if (eviction)
    {
      if (!printed_once)
      {
        cout << "EVICTIONS NEEDED\n";
        printed_once = true;
      }
      // Decide which page to evict (do NOT allow huge page evictions)
      if (policy == LRU || policy == LRU_HALF || policy == LFU || policy == FIFO) 
      {
        c = tail->prev; // LRU, LFU
        while (c->isHugePage)
        {
          c = c->prev;
        }
        assert(c != head);
        map_evict(c, dirtyEvict, evictedAddr, evictedOffset);
        deleteNode(c);
      }
    }
    else
    { // there is space, no need for eviction
      c = freeEntries.back();

      // this should always be false for Hawkeye policy (and other applicaton-agnostic protocols)
      // only do this if you have a user-aware protocol, so they can manually promote a huge page
      if (is2M)
      {
        // 1 huge page = 512 regular pages
        // cout << "Huge page brought in\n";
        for (int i = 0; i < 512; i++)
        {
          freeEntries.pop_back();
        }
      }
      else
      {
        freeEntries.pop_back();
      }
    }

    if (!((policy == CLOCK || policy == WS_CLOCK) && eviction))
    {
      assert(c != NULL);
      addr_map[address] = c; // insert into address map
      c->addr = address;
      c->offset = offset;
      c->dirty = !isLoad; // write-back cache insertion
      c->isHugePage = is2M;
    }

    if (policy == LRU)
      insertFront(c, head); // LRU for insertion
    else if (policy == LRU_HALF)
      insertHalf(c, head); // insert halfway into set
    else if (policy == LFU)
      insertLFU(c, head); // insert by frequency
    else if (policy == CLOCK || policy == WS_CLOCK)
    {
      if (eviction && policy == CLOCK)
      {
        swapClock(address, offset, isLoad, dirtyEvict, evictedAddr, evictedOffset); // evict and insert by Clock (second chance)
      }
      else if (eviction && policy == WS_CLOCK)
      {
        swapWSClock(address, offset, isLoad, dirtyEvict, evictedAddr, evictedOffset);
      }
      else
        insertClock(c); // just insert by Clock
    }
    else if (policy == FIFO)
    {
      // handle Fifo
      insertFIFO(c);
    }
  }

  // this method can be used to count the number of regular sized pages that can be evicted
  // if the corresponding huge page is brought in
  int count_pages(CacheLine *c, uint64_t tag)
  {
    // TODO
    return 0;
  }

  // this method can be used to discard 4kb pages matching this tag, in the case that the corresponding
  // huge page is about to be brought in
  void discard_pages(CacheLine *c, uint64_t tag)
  {
    // TODO
  }

  // helper method to clear entry from address map
  void map_evict(CacheLine *c, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset)
  {
    addr_map.erase(c->addr);
    *evictedAddr = c->addr;
    *evictedOffset = c->offset;
    *dirtyEvict = c->dirty;
  }

  void evict(uint64_t address, bool *dirtyEvict)
  {
    CacheLine *c = addr_map[address];
    assert(c != head);

    addr_map.erase(c->addr); // removing tag
    *dirtyEvict = c->dirty;
    deleteNode(c);
    freeEntries.push_back(c);
  }

  unsigned short get_freq(uint64_t address)
  {
    assert(addr_map.find(address) != addr_map.end());

    CacheLine *c = addr_map[address];
    assert(c != head);

    return (c->freq);
  }

  void decay_freqs()
  {
    CacheLine *c = head->next;
    while (c != tail)
    {
      c->freq /= 2;
      c = c->next;
    }
  }

  void flush()
  {
    CacheLine *c = head->next;
    while (c != tail)
    {
      addr_map.erase(c->addr); // removing tag
      freeEntries.push_back(c);
      c = c->next;
      deleteNode(c->prev);
    }
  }

  void print()
  {
    CacheLine *c = head->next;
    while (c != tail)
    {
      cout << c->addr << " ";
      // cout << c->addr << endl;
      c = c->next;
    }
    cout << endl;
  }

  void get_set(vector<uint64_t> *addresses)
  {
    CacheLine *c = head->next;
    while (c != tail)
    {
      addresses->push_back(c->addr);
      c = c->next;
    }
  }

  // Insert such that MRU is first
  void insertFront(CacheLine *c, CacheLine *currHead)
  {
    c->next = currHead->next;
    c->prev = currHead;
    currHead->next = c;
    c->next->prev = c;
  }

  // Insert such that MRU is mid-way
  void insertHalf(CacheLine *c, CacheLine *currHead)
  {
    unsigned int idx = 0, num_entries = associativity - freeEntries.size(), end = 0;
    CacheLine *curr = head->next;

    if (num_entries == associativity)
      end = (unsigned int)associativity / 2;
    else
      end = (unsigned int)(num_entries / 2); // insert halfway

    while (idx < end && curr != tail)
    {
      curr = curr->next;
      idx++;
    }

    c->next = curr;
    c->prev = curr->prev;
    curr->prev = c;
    c->prev->next = c;
  }

  void insertLFU(CacheLine *c, CacheLine *currHead)
  {
    CacheLine *curr = head->next;

    while (c->freq < curr->freq)
    {
      curr = curr->next;
    }

    c->next = curr;
    c->prev = curr->prev;
    curr->prev = c;
    c->prev->next = c;
  }

  void deleteNode(CacheLine *c)
  {
    assert(c->next);
    assert(c->prev);
    c->prev->next = c->next;
    c->next->prev = c->prev;
  }

  /**
   * This method is only called when the cache is not yet filled. The Clock
   * algo just needs to add another node to the circularly linked list and
   * set the clock hand to this new node.
   */
  void insertClock(CacheLine *c)
  {
    c->next = tail;
    tail->prev = c;
    c->prev = clockPointer;
    clockPointer->next = c;
    clockPointer = clockPointer->next;
  }

  /**
   * This method iterates through the circular linked list (i.e. the "clock")
   * and finds the first node with a clear dirty bit. Forßa given node, if it's
   * dirty bit is set, we clear it and continue.
   */
  void swapClock(uint64_t address, uint64_t offset, bool isLoad, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset)
  {
    // clockPointer will store the most recently used page (which means the
    // next page is the oldest page)
    clockPointer = clockPointer->next;

    while (clockPointer->dirty || clockPointer == head || clockPointer == tail)
    {
      clockPointer->dirty = false;
      clockPointer = clockPointer->next;
    }

    // remove evicted node from address map
    map_evict(clockPointer, dirtyEvict, evictedAddr, evictedOffset);

    // swap info in clockPointer
    clockPointer->addr = address;
    clockPointer->offset = offset;
    clockPointer->dirty = !isLoad;

    // add updated into back into address map
    addr_map[address] = clockPointer;
  }

  // Similar to swapClock(), but this implementation considers the timer and tau
  void swapWSClock(uint64_t address, uint64_t offset, bool isLoad, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset)
  {
    bool checkFreq = true;
    CacheLine *first = clockPointer;
    clockPointer = clockPointer->next;

    // add check for not the first entry here (in the case where all timers are less than tau)
    while (clockPointer->dirty || clockPointer == head || clockPointer == tail || (checkFreq && clockPointer->timer < TAU_WSCLOCK))
    {
      clockPointer->dirty = false;
      clockPointer = clockPointer->next;
      if (clockPointer == first)
        checkFreq = false;
    }

    // remove evicted node from address map
    map_evict(clockPointer, dirtyEvict, evictedAddr, evictedOffset);

    // swap info in clockPointer
    clockPointer->addr = address;
    clockPointer->offset = offset;
    clockPointer->dirty = !isLoad;
    
    // set timer to 0
    clockPointer->timer = 0;

    // add updated into back into address map
    addr_map[address] = clockPointer;
  }

  void tick_WSClock()
  {
    CacheLine *curr = head->next;
    while (curr != tail)
    {
      curr->timer++;
      curr = curr->next;
    }
  }

  void insertFIFO(CacheLine *c)
  {
    insertFront(c, head);
  }

  void countAllPages()
  {
    unsigned long num_hugepages = 0;
    unsigned long num_regpages = 0;
    CacheLine *curr = head->next;
    while (curr != tail)
    {
      if (curr->isHugePage)
        num_hugepages++;
      else
        num_regpages++;
      
      curr = curr->next;
    }
    printf("--- page info ---\n");
    printf("number of hugepages = %lu\n", num_hugepages);
    printf("number of 4kb pages = %lu\n", num_regpages);
  }

  bool isMappedToHugePage(uint64_t addr)
  {
    return hugepages.count(addr);
  }

  void update_acv_access(uint64_t addr)
  {
    acvs[addr]++;
  }

  // TODO
  // add a function to go through all pages and remove the 4kb ones that are
  // going to be included in a hugepage promotion

};

class FunctionalCache
{
public:
  int line_count;
  int set_count;
  int log_set_count;
  int cache_line_size;
  int log_line_size;
  vector<CacheSet *> sets;

  FunctionalCache(unsigned long size, int assoc, int line_size, Replacement_Policy eviction_policy, Promotion_Policy promotion_policy=NA)
  {
    cache_line_size = line_size;
    line_count = size / cache_line_size;
    set_count = line_count / assoc;
    log_set_count = log2(set_count);
    log_line_size = log2(cache_line_size);

    for (int i = 0; i < set_count; i++)
    {
      CacheSet *set = new CacheSet(assoc, cache_line_size, eviction_policy, promotion_policy);
      sets.push_back(set);
    }
  }

  void print_page_counts()
  {
    CacheSet *c = sets.at(0); // RAM simulator only has 1 set (fully associative)
    c->countAllPages();
  }

  uint64_t extract(int max, int min, uint64_t address) // inclusive
  {
    uint64_t maxmask = ((uint64_t)1 << (max + 1)) - 1;
    uint64_t minmask = ((uint64_t)1 << (min)) - 1;
    uint64_t mask = maxmask - minmask;
    uint64_t val = address & mask;
    val = val >> min;
    return val;
  }

  bool access(uint64_t address, bool isLoad, bool is2M = false, bool print = false)
  {
    // bool is2mb = false;
    // TODO
    // add check here to see if this is a hugepage access
    // (setid is always gonna be 0)
    int log_pofs = is2M ? log2(SUPERPAGE_SIZE) : log_line_size;
    uint64_t offset = extract(log_pofs - 1, 0, address);
    uint64_t setid = extract(log_set_count + log_pofs - 1, log_pofs, address);
    uint64_t tag = extract(63, log_set_count + log_pofs, address);
    CacheSet *c = sets.at(setid);

    bool res = c->access(tag, offset, isLoad);
    if (print)
      cout << "Accessing " << address << "; tag = " << tag << ", setid = " << setid << ", offset = " << offset << " --> " << res << endl;

    return res;
  }

  void insert(uint64_t address, bool isLoad, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset, bool is2M = false, bool print = false)
  {
    int log_pofs = is2M ? log2(SUPERPAGE_SIZE) : log_line_size;
    uint64_t offset = extract(log_pofs - 1, 0, address);
    uint64_t setid = extract(log_set_count - 1 + log_pofs, log_pofs, address);
    uint64_t tag = extract(63, log_set_count + log_pofs, address);
    CacheSet *c = sets.at(setid);
    int64_t evictedTag = -1;
    *dirtyEvict = false;

    c->insert(tag, offset, isLoad, dirtyEvict, &evictedTag, evictedOffset, is2M);
    if (print)
      cout << "Inserting " << address << "; tag = " << tag << ", setid = " << setid << ", offset = " << offset << endl;

    if (evictedAddr && evictedTag != -1)
    {
      *evictedAddr = evictedTag * set_count + setid;
    }
  }

  void evict(uint64_t address, bool *dirtyEvict, bool is2M = false, bool print = false)
  {
    int log_pofs = is2M ? log2(SUPERPAGE_SIZE) : log_line_size;
    uint64_t offset = extract(log_pofs - 1, 0, address);
    uint64_t setid = extract(log_set_count - 1 + log_pofs, log_pofs, address);
    uint64_t tag = extract(63, log_set_count + log_pofs, address);
    CacheSet *c = sets.at(setid);
    *dirtyEvict = false;

    c->evict(tag, dirtyEvict);
    if (print)
      cout << "Evicting " << address << "; tag = " << tag << ", setid = " << setid << ", offset = " << offset << endl;
  }

  unsigned short getFreq(uint64_t address, bool is2M = false)
  {
    int log_pofs = is2M ? log2(SUPERPAGE_SIZE) : log_line_size;
    uint64_t setid = extract(log_set_count - 1 + log_pofs, log_pofs, address);
    uint64_t tag = extract(63, log_set_count + log_pofs, address);
    CacheSet *c = sets.at(setid);

    return c->get_freq(tag);
  }

  void flush()
  {
    for (int i = 0; i < set_count; i++)
    {
      sets[i]->flush();
    }
  }

  void print()
  {
    for (int i = 0; i < set_count; i++)
    {
      cout << "Set " << i << ":\n";
      sets[i]->print();
    }
  }

  vector<uint64_t> get_entries()
  {
    vector<uint64_t> addresses;
    for (int i = 0; i < set_count; i++)
    {
      sets[i]->get_set(&addresses);
    }
    return addresses;
  }
};
