import matplotlib.pyplot as plt
from matplotlib import cm
import csv
import numpy as np
from scipy.interpolate import make_interp_spline, BSpline
import math

x = []
y = []

total_accesses, successes = 0, 0

with open('hugepage_rris_25.csv', 'r') as csvfile:
    plots = csv.reader(csvfile, delimiter=',')
    next(plots)  # skip header
    for row in plots:
        # print(row)
        rri, num_accesses = int(row[0]), int(row[1])
        if (rri <= 262144):
            successes += num_accesses
        total_accesses += num_accesses
        x.append(rri)
        y.append(num_accesses)


print("total accesses = ", total_accesses)
print("successful accesses = ", successes)
print("rate = ", (successes / total_accesses))
# plt.bar(x, y)

# for smooth curve
plt.yscale("log")
plt.xscale("log")
# plt.xlim(0, 10000)
min_x, max_x = min(x), max(x)
# xnew = np.linspace(min_x, max_x, 500)
# spl = make_interp_spline(x, y, k=3)
# y_smooth = spl(xnew)
# plt.plot(x, y)
# plt.plot(xnew, y_smooth)
# plt.scatter(x, y, s=3, color=cm.rainbow(min_x, max_x))
# s = np.where(np.array(y) > 100000000, 12, 3)
plt.scatter(x, y, s=3)
# plt.scatter(x, y, s=s)
# plt.bar(x, y)

# adding trendline
# z = np.polyfit(x, y, 2)
# p = np.poly1d(z)
# plt.plot(x, p(x), color="purple", linestyle="--")

# plt.plot(x, y)
plt.xlabel('RRI')
plt.ylabel('Num. of Accesses')
plt.title(
    'RRI of each huge page region access during BFS on Kronecker25 [log]')
# plt.legend()
plt.show()
