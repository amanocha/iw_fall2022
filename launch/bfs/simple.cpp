#include "stdio.h"
#include "stdlib.h"
#include "assert.h"
#include <iostream>
#include <string>
#include <fstream>
#include <chrono>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <vector>

#include "bfs.h"

using namespace std;
extern int errno;
bool printing = false;

void print_regions(csr_graph G, unsigned long *ret, unsigned long *in_wl, unsigned long *out_wl) {
  vector<string> data_names{"NODE_ARRAY", "EDGE_ARRAY", "PROP_ARRAY", "IN_WL", "OUT_WL"};
  vector<pair<uint64_t, uint64_t>> mem_regions;

  mem_regions.push_back(make_pair((uint64_t) G.node_array, (uint64_t)(G.node_array+G.nodes+1)));
  mem_regions.push_back(make_pair((uint64_t) G.edge_array, (uint64_t)(G.edge_array+G.edges)));
  mem_regions.push_back(make_pair((uint64_t) ret, (uint64_t)(ret+G.nodes)));
  mem_regions.push_back(make_pair((uint64_t) in_wl, (uint64_t)(in_wl+G.nodes*2)));
  mem_regions.push_back(make_pair((uint64_t) out_wl, (uint64_t)(out_wl+G.nodes*2)));

  uint64_t start, end;
  cout << "\nMemory Regions:" << endl;
  for (unsigned int i = 0; i < mem_regions.size(); i++) {
    start = (uint64_t)(mem_regions[i].first); // align to page
    end = (uint64_t)(mem_regions[i].second);
    cout << data_names[i] << ": starting base = " << start << ", ending base = " << end << endl;

    if (i == 0) {
      node_start = start;
      node_end = end;
    }
    else if (i == 1) {
      edge_start = start;
      edge_end = end;
    }
    else if (i == 2) {
      prop_start = start;
      prop_end = end;
    }
    else if (i == 3) {
      in_wl_start = start;
      in_wl_end = end;
    }
    else if (i == 4) {
      out_wl_start = start;
      out_wl_end = end;
    }
  }
  cout << endl;
}

int main(int argc, char** argv) {
  string graph_fname;
  csr_graph G;
  unsigned long start_seed, in_index, out_index, *ret, *in_wl, *out_wl;
  chrono::time_point<chrono::system_clock> start, end;

  // Parse arguments
  assert(argc >= 2);
  graph_fname = argv[1];
  if (argc >= 11) start_seed = atoi(argv[10]);
  else start_seed = 0; 

  Replacement_Policy policy = LRU;
  string policy_name = argv[2];
  if (policy_name == "LRU")
    policy = LRU;
  else if (policy_name == "LRU_HALF")
    policy = LRU_HALF;
  else if (policy_name == "LFU")
    policy = LFU;
  else if (policy_name == "CLOCK")
    policy = CLOCK;
  else if (policy_name == "FIFO")
    policy = FIFO;
  else if (policy_name == "WS_CLOCK")
    policy = WS_CLOCK;
  else
    cout << "Policy not recognized\n";
  
  Promotion_Policy promotion_policy = NA;
  string promo_name = argv[3];
  if (promo_name == "NA")
    promotion_policy = NA;
  else if (promo_name == "HAWKEYE")
    promotion_policy = HAWKEYE;
  else if (promo_name == "INGENS")
    promotion_policy = INGENS;
  else if (promo_name == "CUSTOM")
    promotion_policy = CUSTOM;
  else
    cout << "Promotion policy not recognized\n";

  User_Aware aware = UNAWARE;
  string is_aware = argv[4];
  if (is_aware == "AWARE")
    aware = AWARE;
  else if (is_aware == "UNAWARE")
    aware = UNAWARE;
  else
    cout << "User aware policy not recognized\n";
  

  
  int hugepage_limit = stoi(argv[5]);
  int tau_promo = stoi(argv[6]);
  string t = argv[7];
  bool is_all_huge = (t == "ALLHUGE");
  cout << argv[7] << endl;
  float ingens_threshold = stof(argv[8]);

  bool track_rri = false;
  if (argc >= 10)
  {
    track_rri = true;
    cout << "Tracking Re-Reference Interval Metrics: ENABLED\n";
  }
  else
  {
    cout << "Tracking Re-Reference Interval Metrics: DISABLED\n";
  }

  // Initialize data and create irregular data
  G = parse_bin_files(graph_fname, 0, 1);
  init_kernel_policy(G.nodes, start_seed, &in_index, &out_index, &in_wl, &out_wl, &ret, policy, promotion_policy, aware, hugepage_limit, tau_promo, ingens_threshold, track_rri);

  // print data structure regions
  if (printing) 
    print_regions(G, ret, in_wl, out_wl);

  // Execute app
  printf("\n\nstarting kernel\n");
  start = chrono::system_clock::now();
  pin_start();
  kernel(G, ret, in_wl, &in_index, out_wl, &out_index, 0, 1, (aware == AWARE), is_all_huge);
  pin_end();
  end = std::chrono::system_clock::now();
  printf("ending kernel\n");
  
  // Measure execution time      
  chrono::duration<double> elapsed_seconds = end-start;
  printf("\ntotal kernel computation time: %f\n", elapsed_seconds.count());

#if defined(OUTPUT_RET)
  ofstream outfile("out.txt");
  for (unsigned long i = 0; i < G.nodes; i++) {
    outfile << ret[i] << "\n";
  }
  outfile.close();
#endif

  // Clean up
  free(ret); 
  free(in_wl);
  free(out_wl);
  clean_csr_graph(G);

  return 0;
}
