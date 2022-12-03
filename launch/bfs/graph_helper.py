import argparse
import numpy as np
import math
import matplotlib.pyplot as plt
import os
import re
import sys
from scipy.stats.mstats import gmean

# EXPERIMENT VARIABLES
apps = ["bfs", "sssp", "pagerank"]
inputs = ["Kronecker_25"]
metrics = ["TLB Miss Rate"]
legend_names = ["4KB Pages", "Linux THP"]
configs = ["none", "thp"]
readdir = "../applications/asplos_results/single_thread/"

# PLOT VARIABLES
plot_name = "tlb_miss_rate"
colors = ['tab:blue', 'tab:orange', 'tab:green', 'tab:red', 'tab:purple', 'tab:brown']
ylabel = 'TLB Miss %'
yticks = np.arange(0, 51, 10)

AXIS_FONTSIZE = 28
TICK_FONTSIZE = 24
INPUTS_FONTSIZE = 20

scale = 1.25
width = 0.95

def parse_info(metric, configs, readdir):
  print("\nPARSING INFO...\n----------")
  runtimes = []

  for a in range(len(apps)):
    app = apps[a]
    for i in range(len(inputs)):
      if len(runtimes) <= i:
        runtimes.append([])
      input = inputs[i].replace("/", "")
      for c in range(len(configs)):
        if (len(runtimes[i])) <= c:
          runtimes[i].append([])
        filename = readdir + configs[c] + "/" + app + "_" + input + "/results.txt"
        print(filename)
          
        if os.path.isfile(filename):
          print("READING: " + filename)
          measurements = open(filename)
          data = measurements.read()
          measurements.close()
          matches = re.findall(metric + "\s*.*: (\d+\.*\d+(?:e\+*-*\d+)*)", data, re.MULTILINE)
          runtime = float(matches[0]) if len(matches) != 0 else 0
          runtimes[i][c].append(runtime)
        else:
          runtimes[i][c].append(0)

  print("\nData:")
  for i in range(len(runtimes)): #inputs
    for c in range(len(runtimes[i])): #configs
      print(i, c, runtimes[i][c])
  print("\n")

  return runtimes

def compute_averages(input, add=False):
  print("\nCALCULATING AVERAGES...\n----------")
  averages = []

  for c in range(len(input[0])): #configs
    averages.append([])
    for a in range(len(input[0][c])): #apps
      if len(averages[c]) <= a:
        averages[c].append(1)
      if add:
        for i in range(len(input)): #inputs
          averages[c][a] = averages[c][a] + input[i][c][a]
        averages[c][a] = averages[c][a]/len(input)
      else:
        for i in range(len(input)): #inputs
          averages[c][a] = averages[c][a] * input[i][c][a]
        averages[c][a] = math.pow(averages[c][a], 1.0/len(input))

  print("Averages:")
  for c in range(len(averages)): #configs
    print(c, averages[c])
  print("\n")

  return averages

def create_apps_axis(ax1, ind, yticks, ylabel, avg):
  num_apps = len(apps)
  num_inputs = len(inputs) 
  if avg:
    num_inputs = num_inputs + 1
  input_labels = inputs * num_apps

  input_inds = []
  seps = []
  for i in ind:
    pos = []
    for n in range(num_inputs):
      if num_inputs % 2 == 0:
        offset = n-num_inputs/2+0.5
      else:
        offset = n-(num_inputs-1)/2
      pos.append(i + offset*width/num_inputs)
    input_inds = input_inds + pos
    if i == ind[len(ind)-1]:
      continue
    else:
      seps = seps + [i + 0.5]

  ax2 = ax1.twiny()
  ax3 = ax1.twiny()

  ax1.set_xlim([0.5, num_apps+0.5])
  ax1.set_xticks(input_inds)
  ax1.set_xticklabels(input_labels, fontsize=scale*INPUTS_FONTSIZE, rotation=0)
  ax1.set_yticks(yticks)
  ax1.set_yticklabels(yticks, fontsize=scale*TICK_FONTSIZE)
  ax1.set_ylim([yticks[0], yticks[len(yticks)-1]])
  ax1.set_ylabel(ylabel, fontsize=scale*AXIS_FONTSIZE)
  ax1.tick_params(direction='inout', length=20, width=1)
  ax1.tick_params(axis='y', labelsize=scale*TICK_FONTSIZE)

  ax2.set_xlim([0.5, num_apps+0.5])
  ax2.xaxis.set_ticks_position("bottom")
  ax2.xaxis.set_label_position("bottom")
  ax2.spines["bottom"].set_position(("axes", -0.16))
  ax2.spines["bottom"].set_visible(False)
  ax2.set_xticks(ind)
  ax2.set_xticklabels(xticks, fontsize=scale*AXIS_FONTSIZE)
  ax2.tick_params(length=0)

  ax3.set_xlim([0.5, num_apps+0.5])
  ax3.xaxis.set_ticks_position("bottom")
  ax3.set_xticks(seps)
  ax3.set_xticklabels([])
  ax3.tick_params(direction='inout', length=40, width=1.25)

def plot_stacked(filename, stats, averages, colors, outdir, yticks, ylabel, avg, size=None):
  ind = np.arange(1, len(apps)+1)
  num_inputs = len(inputs) 
  if avg:
    num_inputs = num_inputs + 1

  N = num_inputs

  if not size:
    size = (55.0,10.0)
  fig = plt.figure(figsize=size)
  fig.subplots_adjust(bottom=0.1)
  ax1 = fig.add_subplot(111)

  num_cols = 1
  num_configs = len(legend_names)

  psbs = []
  for i in range(N):
    index = int(i/num_cols/num_configs)
    n = i % num_cols
    group = i % (num_cols*num_configs)
    config = int(group/num_cols)
    offset = float(num_inputs-1)/2

    if num_inputs*num_configs % 2 == 0:
      pos = int(i/num_cols)-num_inputs*num_configs/2+0.5+(index-offset)
    else:
      pos = int(i/num_cols)-num_inputs*num_configs/2+(index-offset)

    if avg and index == (num_inputs - 1):
      data = averages[n][config]
    else:
      data = stats[index][n][config]

    psbs.append(ax1.bar(ind+pos*width/((num_configs+1)*num_inputs), data, width/((num_configs+1)*num_inputs), color=colors[group], linewidth=1, edgecolor=['black']))

  create_apps_axis(ax1, ind, yticks, ylabel, avg)

  #plt.show()
  plt.savefig(outdir + filename + ".png", bbox_inches='tight')

def plot():
  outdir = "figs/"

  data = parse_info(metrics[0], configs, readdir)
  averages = compute_averages(data, True)

  plot_stacked(plot_name, data, averages, colors, outdir, yticks, ylabel, True, size = (25.0, 6.0))

  print("\nDone!")

if __name__ == "__main__":
  plot()