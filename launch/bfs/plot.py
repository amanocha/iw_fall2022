import matplotlib.pyplot as plt
import matplotlib.ticker as mtick
from matplotlib import cm
import csv
import numpy as np
from scipy.interpolate import make_interp_spline, BSpline
import math

x = []
y = []
z = []
ACCESSES = 2179655452.0

with open('cycles_thresholds.csv', 'r') as csvfile:
    plots = csv.reader(csvfile, delimiter=',')
    next(plots)  # skip header
    for row in plots:
        threshold = float(row[0])
        user_aware = (row[2] == "TRUE")
        cycles = int(row[3])
        misses = int(row[5])
        avg_promos = float(row[7])
        page_evictions, huge_evictions = int(row[8]), int(row[9])
        miss_rate = (float(misses) / ACCESSES)

        if ((not user_aware) and cycles == 10000):
            x.append(threshold)
            y.append(miss_rate)
            z.append(huge_evictions)


plt.yscale("linear")
plt.xscale("linear")
# plt.ylim(0.0078, 0.009)

# PLOTTING MISSS RATE vs. THRESHOLD
plt.plot(x, y, color='b', label="miss rates")
colors = np.where(np.array(z) > 0, 'r', 'g')
plt.scatter(x, y, s=10, color=colors)
plt.xlabel('Threshold ' + r'$\tau$')
plt.ylabel('Page fault rate')
plt.axvline(x=0.1250, color='r', linestyle='dashed',
            label='huge page eviction cutoff')
plt.title(
    'Page fault rate against thresholds (' + r'$\alpha$' + ' = 2500)', fontsize=12)

# PLOTTING THRESHOLD vs. MISS RATE


plt.legend()
plt.show()
