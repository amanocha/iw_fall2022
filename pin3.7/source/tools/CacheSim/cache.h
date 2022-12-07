#include <unordered_map>
#include <unordered_set>
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
#define TAU_WSCLOCK 5

// Bounds for CUSTOM promotion policy
#define TAU_PROMOTION 5000
#define HUGEPAGE_LIMIT 50

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
  std::unordered_set<uint64_t> hugepages;
  int access_cycles = TAU_PROMOTION;
  int num_available_pages; // tracks the current number of available 4kb pages

  unsigned int associativity;
  int line_size;
  unsigned policy;
  unsigned promotion_policy;

  CacheSet(int size, int cache_line_size, Replacement_Policy eviction_policy, Promotion_Policy promotion_pol)
  {
    associativity = size;
    num_available_pages = size;
    line_size = cache_line_size;
    policy = eviction_policy;
    promotion_policy = promotion_pol;
    cout << "policy = " << policy << "\n";
    cout << "promotion policy = " << promotion_policy << "\n";

    CacheLine *c;
    entries = new CacheLine[associativity];
    if (promotion_policy != CUSTOM)
    {
      for (unsigned int i = 0; i < associativity; i++)
      {
        c = &entries[i];
        freeEntries.push_back(c);
      }
    }
    // if (promotion_policy == CUSTOM)
    // {
    //   CacheLine *c;
    //   for (int i = 0; i < HUGEPAGE_LIMIT; i++)
    //   {
    //     c = freeEntries.back();
    //     freeEntries.pop_back();
    //     c->isHugePage = true;
    //     freeHugepageEntries.push_back(c);
    //   }
    // }
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
  bool access(uint64_t address, uint64_t hugepage_address, uint64_t offset, bool isLoad)
  {
    // cout << "accesses are still happening " << access_cycles << "\n";
    if (promotion_policy == CUSTOM)
    {
      access_cycles--;
      if (access_cycles < 0)
      {
        cout << "rebalancing hugepages\n";
        rebalance_hugepages();
        access_cycles = TAU_PROMOTION;
      }
    }
    CacheLine *c = isMappedToHugePage(hugepage_address) ? addr_map[hugepage_address] : addr_map[address];
    if (c)
    { // Hit

      // cout << "cache hit\n";

      // policy 3 == Clock Algo, 4 == FIFO
      // only want to delete if policy is not Clock Algo or FIFO
      if (policy == LRU || policy == LRU_HALF || policy == LFU)
        deleteNode(c);

      if (policy == LRU)
      {
        // cout << "inserting LRU\n";
        insertFront(c, head);
        // cout << "done inserting LRU\n";
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

      // update acvs
      update_acv_access(hugepage_address);
      // cout << "updated acv accesses\n";
      
      // also add periodic "upgrades" every n accesses to promote/demote pages using the policy
      // this effectively behaves as the daemon thread for promotions

      if (!isLoad)
        c->dirty = true;

      c->offset = offset;
      c->freq++;
      // if (c->freq == MAX_FREQ)
      //   decay_freqs();
      return true;
    }
    else
    {
      return false;
    }
  }

  template<typename K, typename V>
  void print_map(std::unordered_map<K, V> const &m)
  {
    for (auto const &pair: m) {
        std::cout << "{" << pair.first << ": " << pair.second << "}\n";
    }
  }

  void rebalance_hugepages()
  {
    // PART 1
    // find the regions that are meant to be promoted
    int size = (HUGEPAGE_LIMIT > acvs.size()) ? acvs.size() : HUGEPAGE_LIMIT;
    std::vector<pair<uint64_t, int>> top_n(size);
    std::partial_sort_copy(acvs.begin(), acvs.end(), top_n.begin(), top_n.end(),
                          [](pair<uint64_t, int> const& l,
                              pair<uint64_t, int> const& r)
                          {
                              return l.second > r.second;
                          });
    std::unordered_set<uint64_t> next_hugepages;
    print_map(acvs);
    acvs.clear();

    for (std::pair<uint64_t, int> p : top_n)
    {
      uint64_t hugepage_addr = p.first;
      next_hugepages.insert(hugepage_addr);
      hugepages.erase(hugepage_addr);
    }

    // PART 2
    // evict and demote hugepages that did not meet the cutoff
    for (auto p : hugepages)
    {
      evict(p);
    }

    // PART 3
    // delete nodes that will be brought in when huge pages are added
    remove_4kb_pages(next_hugepages);

    // PART 4
    // insert huge page regions
    for (auto p : next_hugepages)
    {
      bool isLoad, *dirtyEvict;
      int64_t *evictedAddr, evictedTag = -1;
      uint64_t *evictedOffset, offset;
      insert(p, 0, isLoad, dirtyEvict, &evictedTag, evictedOffset, true);
    }
    hugepages = next_hugepages;
    cout << "done rebalancing hugepages\n";
    print_map(addr_map);
  }

  // function to go through all pages and remove the 4kb ones that are
  // going to be included in a hugepage promotion
  void remove_4kb_pages(unordered_set<uint64_t> hugepage_addrs)
  {
    CacheLine *c = head->next;
    while (c != tail)
    {
      if (c->isHugePage)
      {
        c = c->next;
        continue;
      }

      // check if 4kb tag will be contained in hugepage
      bool to_evict = (hugepage_addrs.count((c->addr >> 9)) > 0);
      if (to_evict)
      {
        CacheLine *temp = c;
        c = c->next;
        map_evict(temp);
        deleteNode(temp);
        num_available_pages++;
      }
      else
      {
        c = c->next;
      }
    }
  }

  void insert(uint64_t address, uint64_t offset, bool isLoad, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset, bool is2M=false)
  {
    CacheLine *c = addr_map[address];
    // bool eviction = is2M ? (freeHugepageEntries.size() < 0) : (freeEntries.size() == 0);
    bool eviction = is2M ? (num_available_pages < 512) : (num_available_pages == 0);

    if (eviction)
    {
      if (!printed_once)
      {
        cout << "EVICTIONS NEEDED\n";
        printed_once = true;
      }
      if (promotion_policy == CUSTOM && is2M)
      {
        cout << "need evictions to promote hugepage\n";
      }
      // Decide which page to evict (do NOT allow huge page evictions)
      if (policy == LRU || policy == LRU_HALF || policy == LFU || policy == FIFO) 
      {
        if (promotion_policy == CUSTOM)
        {
          int space_needed = is2M ? 512 : 1;
          while (num_available_pages < space_needed)
          {
            evict_LRU_node();
          }
          c = new CacheLine;
        }
        else
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
    }
    else
    { // there is space, no need for eviction
      if (promotion_policy == CUSTOM)
      {
        c = new CacheLine;
        num_available_pages -= is2M ? 512 : 1;
      }
      else
      {
        c = freeEntries.back();
        freeEntries.pop_back();
      }

      // this should always be false for Hawkeye policy (and other applicaton-agnostic protocols)
      // only do this if you have a user-aware protocol, so they can manually promote a huge page
    }

    if (!((policy == CLOCK || policy == WS_CLOCK) && eviction))
    {
      assert(c != NULL);
      addr_map[address] = c; // insert into address map
      c->addr = address;
      c->offset = offset;
      c->dirty = !isLoad; // write-back cache insertion
      c->isHugePage = is2M;
      // std::cout << "c->isHugePage = " << c->isHugePage << "\n";
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

  void evict_LRU_node()
  {
    CacheLine *c = tail->prev;
    // while (c->isHugePage)
    // {
    //   c = c->prev;
    // }
    assert(c != head);
    if (c->isHugePage)
    {
      num_available_pages += 512;
      cout << "evicted a hugepage\n";
    }
    else
    {
      num_available_pages++;
    }
    map_evict(c);
    deleteNode(c);
  }

  // this method can be used to count the number of regular sized pages that can be evicted
  // if the corresponding huge page is brought in
  int count_pages(CacheLine *c, uint64_t tag)
  {
    // TODO
    return 0;
  }

  // helper method to clear entry from address map
  void map_evict(CacheLine *c, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset)
  {
    addr_map.erase(c->addr);
    *evictedAddr = c->addr;
    *evictedOffset = c->offset;
    *dirtyEvict = c->dirty;
  }

  void map_evict(CacheLine *c)
  {
    addr_map.erase(c->addr);
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
  
  void evict(uint64_t address)
  {
    CacheLine *c = addr_map[address];
    assert(c != head);
    assert(c != tail);

    addr_map.erase(c->addr);
    deleteNode(c);
    if (promotion_policy == CUSTOM)
    {
      num_available_pages += (c->isHugePage) ? 512 : 1;
    }
    else
    {
      freeEntries.push_back(c);
    }
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
   * and finds the first node with a clear dirty bit. For a given node, if it's
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

  bool isMappedToHugePage(uint64_t hugepage_addr)
  {
    return (hugepages.count(hugepage_addr) > 0);
  }

  void update_acv_access(uint64_t hugepage_addr)
  {
    if (promotion_policy == CUSTOM)
    {
      if (acvs.count(hugepage_addr) == 0)
      {
        acvs[hugepage_addr] = 2;
      }
      else
      {
        acvs[hugepage_addr]++;
      }
    }
  }

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

  FunctionalCache(unsigned long size, int assoc, int line_size, Replacement_Policy eviction_policy, Promotion_Policy promotion_policy = NA)
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

    // HUGEPAGE TAG
    int log_pofs_hp = log2(SUPERPAGE_SIZE);
    uint64_t offset_hp = extract(log_pofs_hp - 1, 0, address);
    uint64_t setid_hp = extract(log_set_count + log_pofs_hp - 1, log_pofs_hp, address);
    uint64_t tag_hp = extract(63, log_set_count + log_pofs_hp, address);

    // REGULAR PAGE TAG
    int log_pofs = log_line_size;
    uint64_t offset = extract(log_pofs - 1, 0, address);
    uint64_t setid = extract(log_set_count + log_pofs - 1, log_pofs, address);
    uint64_t tag = extract(63, log_set_count + log_pofs, address);


    CacheSet *c = sets.at(0);

    bool res = c->access(tag, tag_hp, offset, isLoad);
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
