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
}

void kernel(csr_graph G, unsigned long *ret, unsigned long *in_wl, unsigned long *in_index, unsigned long *out_wl, unsigned long *out_index, int tid, int num_threads)
{

  int hop = 1;
  while (*in_index > 0)
  {
    printf("-- epoch %d %lu --> push\n", hop, *in_index);
    for (unsigned long i = tid; i < *in_index; i += num_threads)
    {
      unsigned long node = in_wl[i];
      uint64_t vaddr = (uint64_t)(&in_wl[i]);
      printf("vaddr = %lu", vaddr);
      track_access((uint64_t)(&in_wl[i]));
      unsigned long start = G.node_array[node];   // starting position
      unsigned long end = G.node_array[node + 1]; // ending position

      // track_access((uint64_t)&G.node_array[node]); // do i even need these?
      // track_access((uint64_t)&G.node_array[node + 1]);

      for (unsigned long e = start; e < end; e++)
      {
        unsigned long edge_index = G.edge_array[e];
        unsigned long v = ret[edge_index]; // PROBLEM
        // track_access((uint64_t)&ret[edge_index]);
        if (v == -1)
        {
          ret[edge_index] = hop;
          unsigned long index = *out_index;
          *out_index = *out_index + 1;
          out_wl[index] = edge_index;
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
}
