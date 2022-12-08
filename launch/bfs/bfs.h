#include "stdio.h"
#include "stdlib.h"
#include "assert.h"
#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include "../../utils/common.h"
#include "../../utils/split_huge_page.h"
#include "../../pin3.7/source/tools/CacheSim/track_access.h"

void init_kernel_policy(int num_nodes, int start_seed, unsigned long *in_index, unsigned long *out_index, unsigned long **in_wl, unsigned long **out_wl, unsigned long **ret, Replacement_Policy policy, Promotion_Policy promotion_policy, User_Aware aware, int hugepage_limit, int tau_promo)
{
  *ret = (unsigned long *)malloc(sizeof(unsigned long) * num_nodes);
  *in_wl = (unsigned long *)malloc(sizeof(unsigned long) * num_nodes * 2);
  *out_wl = (unsigned long *)malloc(sizeof(unsigned long) * num_nodes * 2);

  for (unsigned long i = 0; i < num_nodes; i++)
  {
    (*ret)[i] = -1;
  }

  *in_index = 0;
  *out_index = 0;

  for (unsigned long i = start_seed; i < start_seed + SEEDS; i++)
  {
    unsigned long index = *in_index;
    *in_index = index + 1;
    (*in_wl)[index] = i;
    (*ret)[i] = 0;
  }

  init_cache(policy, promotion_policy, aware, hugepage_limit, tau_promo);
}

void init_kernel(int num_nodes, int start_seed, unsigned long *in_index, unsigned long *out_index, unsigned long **in_wl, unsigned long **out_wl, unsigned long **ret)
{
  *ret = (unsigned long *)malloc(sizeof(unsigned long) * num_nodes);
  *in_wl = (unsigned long *)malloc(sizeof(unsigned long) * num_nodes * 2);
  *out_wl = (unsigned long *)malloc(sizeof(unsigned long) * num_nodes * 2);

  for (unsigned long i = 0; i < num_nodes; i++)
  {
    (*ret)[i] = -1;
  }

  *in_index = 0;
  *out_index = 0;

  for (unsigned long i = start_seed; i < start_seed + SEEDS; i++)
  {
    unsigned long index = *in_index;
    *in_index = index + 1;
    (*in_wl)[index] = i;
    (*ret)[i] = 0;
  }

  init_cache();
}

void kernel(csr_graph G, unsigned long *ret, unsigned long *in_wl, unsigned long *in_index, unsigned long *out_wl, unsigned long *out_index, int tid, int num_threads, bool user_aware = false)
{

  int hop = 1;
  bool print_ten = 10;
  while (*in_index > 0)
  {
    printf("-- epoch %d %lu --> push\n", hop, *in_index);
    for (unsigned long i = tid; i < *in_index; i += num_threads)
    {
      unsigned long node = in_wl[i];
      track_access((uint64_t)(&in_wl[i]));

      unsigned long start = G.node_array[node];   // starting position
      unsigned long end = G.node_array[node + 1]; // ending position
      track_access((uint64_t)&G.node_array[node]);
      track_access((uint64_t)&G.node_array[node + 1]);

      for (unsigned long e = start; e < end; e++)
      {
        unsigned long edge_index = G.edge_array[e];
        track_access((uint64_t)&G.edge_array[e]);
        unsigned long v = ret[edge_index]; // this is the problematic access
        // track_access((uint64_t)&ret[edge_index], true);
        track_access((uint64_t)&ret[edge_index], user_aware);
        if (v == -1)
        {
          ret[edge_index] = hop;
          // track_access((uint64_t)&ret[edge_index], true);
          track_access((uint64_t)&ret[edge_index], user_aware);
          unsigned long index = *out_index;
          *out_index = *out_index + 1;
          out_wl[index] = edge_index;
          track_access((uint64_t)&out_wl[index]);
        }
      }
    }

    unsigned long *tmp = out_wl;
    out_wl = in_wl;
    in_wl = tmp;
    hop++;
    *in_index = *out_index;
    *out_index = 0;
  }

  // print results from track_access
  printf("--- all access info --- ");
  printf("\nmemory accesses = %lu\n", total_num_accesses);
  printf("cache hits = %lu\n", num_hits);
  printf("cache misses = %lu\n", num_misses);
  float rate = (100.0 * ((float) num_misses) / ((float) total_num_accesses));
  printf("miss rate = %.8f\n", rate);
  printf("--- eviction info by data structure ---\n");
  printf("node array evictions = %lu\n", node_array_evicts);
  printf("edge array evictions = %lu\n", edge_array_evicts);
  printf("prop array evictions = %lu\n", prop_array_evicts);
  printf("in wl evictions = %lu\n", in_wl_evicts);
  printf("out wl evictions = %lu\n", out_wl_evicts);
  printf("--- access info by data structure ---\n");
  printf("node array accesses = %lu\n", node_array_accesses);
  printf("edge array accesses = %lu\n", edge_array_accesses);
  printf("prop array accesses = %lu\n", prop_array_accesses);
  printf("in wl accesses = %lu\n", in_wl_accesses);
  printf("out wl accesses = %lu\n", out_wl_accesses);
  print_pages_end();
}
