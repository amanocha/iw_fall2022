import matplotlib.pyplot as plt
import matplotlib.ticker as mtick
from matplotlib import cm
import csv
import numpy as np
from scipy.interpolate import make_interp_spline, BSpline
import math

x, x2 = [], []
x_5000, x_10k, x_2500 = [], [], []
y, y2 = [], []
z, z2 = [], []
y_5000, y_10k, y_2500, = [], [], []
w, w2 = [], []
avg_p, avg_p2 = [], []
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

        if ((not user_aware) and cycles == 5000):
            x.append(threshold)
            x_5000.append(threshold)
            y_5000.append(avg_promos)
            y.append(miss_rate)
            z.append(huge_evictions)
            w.append(page_evictions)
            avg_p.append(avg_promos)
        elif (user_aware and cycles == 5000):
            x2.append(threshold)
            y2.append(miss_rate)
            z2.append(huge_evictions)
            w2.append(page_evictions)
            avg_p2.append(avg_promos)
        elif ((not user_aware) and cycles == 2500):
            x_2500.append(threshold)
            y_2500.append(avg_promos)
        elif ((not user_aware) and cycles == 10000):
            x_10k.append(threshold)
            y_10k.append(avg_promos)


plt.yscale("linear")
plt.xscale("linear")
# plt.ylim(0.0078, 0.009)

# PLOTTING MISSS RATE vs. THRESHOLD
# plt.gca().yaxis.set_major_formatter(mtick.PercentFormatter(xmax=1.0))
# plt.plot(x, y, color='b', label="custom policy")
# plt.plot(x2, y2, color='g', label="hybrid policy")
# colors = np.where(np.array(z) > 0, 'r', 'g')
# colors2 = np.where(np.array(z2) > 0, 'r', 'g')
# plt.scatter(x, y, s=10, color='b')
# plt.scatter(x2, y2, s=10, color='g')
# plt.xlabel('Threshold ' + r'$\tau$')
# plt.ylabel('Page fault rate')
# # plt.axvline(x=0.0750, color='r', linestyle='dashed',
# #             label='huge page eviction cutoff')
# plt.title(
#     'Page fault rate against thresholds (' + r'$\alpha$' + ' = 5000)', fontsize=15)

# PLOTTING THRESHOLD vs. PAGE EVICTIONS
# plt.plot(x, z, color='r', label="custom policy")
# plt.plot(x2, z2, color='g', label="hybrid policy")
# plt.scatter(x, z, s=10, color='r')
# plt.scatter(x2, z2, s=10, color='g')
# plt.xlabel('Threshold ' + r'$\tau$')
# plt.ylabel('Evictions')
# # plt.axvline(x=0.125, color='b', linestyle='dashed',
# #             label='no huge page evictions')
# plt.title(
#     'Huge page evictions against thresholds (' + r'$\alpha$' + ' = 5000)', fontsize=12)

# PLOTTING THRESHOLD vs. PAGE EVICTIONS
# plt.plot(x, w, color='b', label="custom policy")
# plt.plot(x2, w2, color='g', label="hybrid policy")
# plt.scatter(x, w, s=10, color='b')
# plt.scatter(x2, w2, s=10, color='g')
# plt.xlabel('Threshold ' + r'$\tau$')
# plt.ylabel('Evictions')
# plt.title(
#     'Baseline page evictions against thresholds (' + r'$\alpha$' + ' = 5000)', fontsize=12)


# PLOT average promotions per cycle
plt.plot(x_2500, y_2500, color='r', label=r'$\alpha$' + ' = 2500')
plt.scatter(x_2500, y_2500, s=3, color='r')
plt.plot(x_5000, y_5000, color='b', label=r'$\alpha$' + ' = 5000')
plt.scatter(x_5000, y_5000, s=3, color='b')
plt.plot(x_10k, y_10k, color='g', label=r'$\alpha$' + ' = 10000')
plt.scatter(x_10k, y_10k, s=3, color='g')
# plt.plot(x2, avg_p2)
# plt.scatter(x2, avg_p2, s=3)
plt.xlabel('Threshold ' + r'$\tau$')
plt.ylabel('Avg. promotions per cycle')
plt.title(
    'Average page promotions across thresholds', fontsize=12)

plt.legend()
plt.show()
