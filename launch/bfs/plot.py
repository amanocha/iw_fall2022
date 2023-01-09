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
w = []
avg_p = []
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
            w.append(page_evictions)
            avg_p.append(avg_promos)


plt.yscale("linear")
plt.xscale("linear")
# plt.ylim(0.0078, 0.009)

# PLOTTING MISSS RATE vs. THRESHOLD
# plt.gca().yaxis.set_major_formatter(mtick.PercentFormatter(xmax=1.0))
# plt.plot(x, y, color='b', label="miss rates")
# colors = np.where(np.array(z) > 0, 'r', 'g')
# plt.scatter(x, y, s=10, color=colors)
# plt.xlabel('Threshold ' + r'$\tau$')
# plt.ylabel('Page fault rate')
# plt.axvline(x=0.125, color='r', linestyle='dashed',
#             label='huge page eviction cutoff')
# plt.title(
#     'Page fault rate against thresholds (' + r'$\alpha$' + ' = 5000)', fontsize=15)

# PLOTTING THRESHOLD vs. PAGE EVICTIONS
plt.plot(x, z, color='r', label="huge page evictions")
plt.scatter(x, z, s=10, color='r')
plt.xlabel('Threshold ' + r'$\tau$')
plt.ylabel('Evictions')
plt.axvline(x=0.2, color='b', linestyle='dashed',
            label='no huge page evictions')
plt.title(
    'Huge page evictions against thresholds (' + r'$\alpha$' + ' = 10000)', fontsize=12)

# PLOTTING THRESHOLD vs. PAGE EVICTIONS
# plt.plot(x, w, color='b', label="baseline page evictions")
# plt.scatter(x, w, s=10, color='b')
# plt.xlabel('Threshold ' + r'$\tau$')
# plt.ylabel('Evictions')
# plt.title(
#     'Baseline page evictions against thresholds (' + r'$\alpha$' + ' = 10000)', fontsize=12)

plt.legend()
plt.show()
