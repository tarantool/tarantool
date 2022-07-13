import matplotlib.pyplot as plt
from scipy.optimize import curve_fit
import numpy as np
import pandas as pd
import sys

"""
This scrip finds bias correction curves using data dumped by
test/unit/hyperloglog.c. Before running the test, enable dumping,
increase number of points (at least 200), specify target range
in the test file and make the algorithm always return raw estimations.
The coefficients of the curves will be written in STDOUT.
"""

if len(sys.argv) != 4:
    print("Usage: {sys.argv[0]} <bias_data_file> <min_prec> <max_prec>")
    sys.exit(1)

min_prec = int(sys.argv[2])
max_prec = int(sys.argv[3])
n_prec = max_prec - min_prec + 1

frame = pd.read_csv(sys.argv[1], sep = ',', skipinitialspace = True)
est_data = list(frame['avg_est'])
card_data = list(frame['card'])

bias_data = list(np.subtract(est_data, card_data))

est_lists = np.array_split(est_data, n_prec)
bias_lists = np.array_split(bias_data, n_prec)

def curve(x, a, b, c, d, e, f):
    return a * (x**5) + b * (x**4) + c * (x**3) + d * (x**2) + e * x + f

for prec in range(min_prec, max_prec + 1):
    idx = prec - min_prec
    coefs, _ = curve_fit(curve, est_lists[idx], bias_lists[idx])

    print(f'/* precision {prec} */',)
    print('{')

    for coef in coefs:
        print(f"\t{coef},")

    print('},')
