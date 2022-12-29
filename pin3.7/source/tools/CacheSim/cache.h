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
#define FREQ_LOWER_BOUND 100

// Bounds for INGENS promotion policy
// (this policy is not actually Ingens, rather a policy inspired by Ingens)
// #define INGENS_THRESHOLD 0.5f

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

enum User_Aware
{
  AWARE,
  UNAWARE
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
  std::unordered_map<uint64_t, int> custom_freqs;
  std::unordered_map<uint64_t, std::bitset<512>> ingens_acvs;
  std::unordered_set<uint64_t> hugepages;
  std::unordered_set<uint64_t> distinct_hugepages;
  unsigned int access_counter = 0;
  unsigned int hugepage_limit = 0;
  unsigned int access_cycles = 0;
  unsigned int tau_promotion = 0;
  unsigned int total_promotions = 0;
  unsigned int prints = 125;
  float ingens_threshold;
  unsigned long num_available_pages = 0, total_inserts = 0, total_evictions = 0; // tracks the current number of available 4kb pages

  unsigned int associativity;
  int line_size;
  unsigned policy;
  unsigned promotion_policy;

  CacheSet(int size, int cache_line_size, Replacement_Policy eviction_policy, Promotion_Policy promotion_pol, User_Aware aware, int hugepage_lim, int tau_p, float ingens_thr)
  {
    associativity = size;
    num_available_pages = size;
    line_size = cache_line_size;
    policy = eviction_policy;
    promotion_policy = promotion_pol;
    hugepage_limit = hugepage_lim;
    tau_promotion = tau_p; // CUSTOM
    access_cycles = tau_promotion;
    ingens_threshold = ingens_thr;
    cout << "policy = " << policy << "\n";
    cout << "promotion policy = " << promotion_policy << "\n";
    cout << "page rebalancing every " << tau_promotion << " cycles\n";
    cout << "number of available 4kb pages = " << num_available_pages << endl;
    if (promotion_pol == INGENS)
      cout << "INGENS_THRESHOLD: " << ingens_threshold << endl;

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
  bool access(uint64_t address, uint64_t hugepage_address, uint64_t offset, bool isLoad)
  {
    distinct_hugepages.insert(hugepage_address);
    access_counter++;
    if (promotion_policy == CUSTOM || promotion_policy == INGENS)
    {
      access_cycles--;
      if (access_cycles < 1)
      {
        if (promotion_policy == CUSTOM)
          rebalance_hugepages_custom();
        else if (promotion_policy == INGENS)
          rebalance_hugepages_ingens();
        access_cycles = tau_promotion;
      }
    }
    CacheLine *c = isMappedToHugePage(hugepage_address) ? addr_map[hugepage_address] : addr_map[address];;
    if (c)
    { // Hit

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

      // update custom_freqs
      update_promotion_access(hugepage_address, address);
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

  void print_vector_pairs(std::vector<pair<uint64_t, int>> output)
  {
    for(int i = 0; i < output.size(); i++)
    {
      cout << output[i].first << ", " << output[i].second << endl;
    }
  }

  void print_set(std::unordered_set<uint64_t> myset)
  {
    cout << "Printing unordered_set\n";
    for (uint64_t a : myset)
    {
      cout << a << endl;
    }
  }

  void rebalance_hugepages_custom()
  {
    if (hugepage_limit == 0)
      return;
    
    // PART 1
    // find the regions that are meant to be promoted
    int size = (hugepage_limit > custom_freqs.size()) ? custom_freqs.size() : hugepage_limit;
    
    std::vector<pair<uint64_t, int>> top_n(size);
    std::partial_sort_copy(custom_freqs.begin(), custom_freqs.end(), top_n.begin(), top_n.end(),
                          [](pair<uint64_t, int> const& l,
                              pair<uint64_t, int> const& r)
                          {
                              return l.second > r.second;
                          });
    std::unordered_set<uint64_t> next_hugepages = std::unordered_set<uint64_t>();
    // print_map(custom_freqs);
    custom_freqs.clear();

    for (std::pair<uint64_t, int> p : top_n)
    {
      if (p.second > FREQ_LOWER_BOUND)
      {
        total_promotions++;
        uint64_t hugepage_addr = p.first;
        next_hugepages.insert(hugepage_addr);
        hugepages.erase(hugepage_addr);
      }
    }

    // PART 2
    // evict and demote hugepages that did not meet the cutoff
    for (uint64_t p : hugepages)
    {
      assert(addr_map.count(p) != 0);
      evict(p);
    }
    
    // PART 3
    // delete nodes that will be brought in when huge pages are added
    remove_4kb_pages(next_hugepages);

    // PART 4
    // insert huge page regions
    for (uint64_t p : next_hugepages)
    {
      bool isLoad, *dirtyEvict;
      int64_t *evictedAddr, evictedTag = -1;
      uint64_t *evictedOffset, offset;
      if (addr_map.count(p) == 0)
      {
        insert(p, 0, isLoad, dirtyEvict, &evictedTag, evictedOffset, true);
      }
    }
    hugepages = next_hugepages;
  }

  void rebalance_hugepages_ingens()
  {
    if (prints > 0)
    {
      cout << "rebalancing pages print " << prints << endl;
    }

    // PART 1
    // find the regions that are meant to be promoted
    int size = (hugepage_limit > ingens_acvs.size()) ? ingens_acvs.size() : hugepage_limit;
    
    std::vector<pair<uint64_t, bitset<512>>> top_n(size);
    std::partial_sort_copy(ingens_acvs.begin(), ingens_acvs.end(), top_n.begin(), top_n.end(),
                          [](pair<uint64_t, bitset<512>> const& l,
                              pair<uint64_t, bitset<512>> const& r)
                          {
                              return l.second.count() > r.second.count();
                          });
    std::unordered_set<uint64_t> next_hugepages = std::unordered_set<uint64_t>();
    // print_map(custom_freqs);
    ingens_acvs.clear();

    for (std::pair<uint64_t, bitset<512>> p : top_n)
    {
      if (prints > 0)
      {
        // cout << "bitset count = " << p.second.count() << endl;
        // cout << "threshold = " << (512.0 * INGENS_THRESHOLD) << endl;
        // cout << ((p.second.count() >= (512.0 * INGENS_THRESHOLD)) ? "true" : "false") << endl;
        prints--;
      }
      if (p.second.count() >= (512.0 * ingens_threshold))
      {
        total_promotions++;
        uint64_t hugepage_addr = p.first;
        next_hugepages.insert(hugepage_addr);
        hugepages.erase(hugepage_addr);
      }
    }

    // PART 2
    // evict and demote hugepages that did not meet the cutoff
    for (uint64_t p : hugepages)
    {
      assert(addr_map.count(p) != 0);
      evict(p);
    }
    
    // PART 3
    // delete nodes that will be brought in when huge pages are added
    remove_4kb_pages(next_hugepages);

    // PART 4
    // insert huge page regions
    for (uint64_t p : next_hugepages)
    {
      bool isLoad, *dirtyEvict;
      int64_t *evictedAddr, evictedTag = -1;
      uint64_t *evictedOffset, offset;
      if (addr_map.count(p) == 0)
      {
        insert(p, 0, isLoad, dirtyEvict, &evictedTag, evictedOffset, true);
      }
    }
    hugepages = next_hugepages;
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
      }
      else
      {
        // check if 4kb tag will be contained in hugepage
        bool to_evict = (hugepage_addrs.count((c->addr >> 9)) > 0);
        if (to_evict)
        {
          CacheLine *temp = c;
          c = c->next;
          map_evict(temp);
          deleteNode(temp);
          freeEntries.push_back(temp);
          num_available_pages++;
        }
        else
        {
          c = c->next;
        } 
      }
    }
  }

  void insert(uint64_t address, uint64_t offset, bool isLoad, bool *dirtyEvict, int64_t *evictedAddr, uint64_t *evictedOffset, bool is2M=false)
  {
    total_inserts++;
    CacheLine *c;
    if (promotion_policy == CUSTOM)
    {
      // c = new CacheLine;
    }
    else
    {
      c = addr_map[address];
    }
    // bool eviction = is2M ? (freeHugepageEntries.size() < 0) : (freeEntries.size() == 0);
    bool eviction = is2M ? (num_available_pages < 512) : (num_available_pages <= 0);

    if (eviction)
    {
      if (!printed_once)
      {
        cout << "EVICTIONS NEEDED\n";
        printed_once = true;
      }
      // Decide which page to evict
      if (policy == LRU || policy == LRU_HALF || policy == LFU || policy == FIFO) 
      {
        int space_needed = is2M ? 512 : 1;
        while (num_available_pages < space_needed)
        {
          evict_LRU_node();
          total_evictions++;
        }
        c = freeEntries.back();
        freeEntries.pop_back();
        num_available_pages -= is2M ? 512 : 1;
      }
    }
    else
    { // there is space, no need for eviction
      num_available_pages -= is2M ? 512 : 1;
      c = freeEntries.back();
      freeEntries.pop_back();
    }

    // if (!((policy == CLOCK || policy == WS_CLOCK) && eviction))
    // {
    assert(c != NULL);
    addr_map[address] = c; // insert into address map
    c->addr = address;
    c->offset = offset;
    c->dirty = !isLoad; // write-back cache insertion
    c->isHugePage = is2M;
    // }

    if (is2M && hugepages.count(address) == 0)
    {
      hugepages.insert(address);
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
      // cout << "evicted a hugepage with address " << c->addr << endl;
    }
    else
    {
      num_available_pages++;
    }
    map_evict(c);
    deleteNode(c);
    freeEntries.push_back(c);
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
    num_available_pages++;
  }
  
  void evict(uint64_t address)
  {
    // cout << "EVICT address: " << address << " from:\n";
    // cout << "START ADDR_MAP ----------\n";
    // print_map(addr_map);
    // cout << "END ADDR_MAP ----------\n";
    assert(addr_map.count(address) > 0);
    CacheLine *c = addr_map[address];
    assert(c != NULL);
    assert(c != head);
    assert(c != tail);
    map_evict(c);
    deleteNode(c);
    num_available_pages += ((c->isHugePage) ? 512 : 1);
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
      num_available_pages++;
      assert(freeEntries.size() == num_available_pages);
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
    unsigned int idx = 0, end = 0;
    unsigned int num_entries = associativity - num_available_pages;
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
      assert(curr != NULL);
      if (curr->isHugePage)
        num_hugepages++;
      else
        num_regpages++;
      
      curr = curr->next;
    }
    printf("--- page info ---\n");
    printf("number of hugepages = %lu\n", num_hugepages);
    printf("number of 4kb pages = %lu\n", num_regpages);
    printf("number of available 4kb pages = %lu\n", num_available_pages);
    printf("total inserts = %lu\n", total_inserts);
    printf("total evictions = %lu\n", total_evictions);
    unsigned int size = distinct_hugepages.size();
    printf("number of distinct hugepages = %d\n", size);
    float f = (float) total_promotions / ((float) access_counter / (float) tau_promotion);
    printf("average number of hugepages per promotion cycle = %.8f\n", f);
    assert(size >= 0);
    assert(access_counter >= 0);
    float rate = 100.0 * ((float) size / (float) access_counter);
    printf("theoretical lower bound on miss rate = %.8f\n", rate);
  }

  bool isMappedToHugePage(uint64_t hugepage_addr)
  {
    // assert(hugepages.count(hugepage_addr) == addr_map.count(hugepage_addr));
    return (addr_map.count(hugepage_addr) > 0);
  }

  void update_promotion_access(uint64_t hugepage_addr, uint64_t address)
  {
    if (promotion_policy == CUSTOM)
    {
      if (custom_freqs.count(hugepage_addr) == 0)
      {
        custom_freqs[hugepage_addr] = 1;
      }
      else
      {
        custom_freqs[hugepage_addr]++;
      }
    }
    else if (promotion_policy == INGENS)
    {
      unsigned int mask = (1 << 9) - 1;
      ingens_acvs[hugepage_addr][(address & mask)] = 1;
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

  FunctionalCache(unsigned long size, int assoc, int line_size, Replacement_Policy eviction_policy, Promotion_Policy promotion_policy = NA, User_Aware aware = UNAWARE, int hugepage_lim = 0, int tau_promo = 0, float ingens_thr = 100.0)
  {
    cache_line_size = line_size;
    line_count = size / cache_line_size;
    set_count = line_count / assoc;
    log_set_count = log2(set_count);
    log_line_size = log2(cache_line_size);

    for (int i = 0; i < set_count; i++)
    {
      CacheSet *set = new CacheSet(assoc, cache_line_size, eviction_policy, promotion_policy, aware, hugepage_lim, tau_promo, ingens_thr);
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
