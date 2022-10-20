import matplotlib.pyplot as plt
from scipy.optimize import curve_fit
from scipy.optimize import root_scalar
import numpy as np
import pandas as pd
import sys

"""
This scrip finds linear counting algorithm thresholds using data dumped by
test/unit/hyperloglog.c. Before running the test, enable dumping,
increase number of points (at least 200), specify target range in the test
file and make the algorithm always return the linear counting estimation.
The thresholds will be written in STDOUT.
"""


if len(sys.argv) != 4:
    print("Usage: {sys.argv[0]} <bias_data_file> <min_prec> <max_prec>")
    sys.exit(1)

min_prec = int(sys.argv[2])
max_prec = int(sys.argv[3])
n_prec = max_prec - min_prec + 1

frame = pd.read_csv(sys.argv[1], sep = ',', skipinitialspace = True)
err_data = list(frame['std_err'])
card_data = list(frame['avg_est'])

err_lists = np.array_split(err_data, n_prec)
card_lists = np.array_split(card_data, n_prec)

def curve(x, a, b, c, d, e, f):
    return a * (x**5) + b * (x**4) + c * (x**3) + d * (x**2) + e * x + f

for prec in range(min_prec, max_prec + 1):
    m = 2 ** prec
    hll_err = 1.04 / np.sqrt(m)
    idx = prec - min_prec
    coefs, _ = curve_fit(curve, card_lists[idx], err_lists[idx])

    error_curve = lambda x : curve(x, *coefs)
    solution = root_scalar(lambda x : error_curve(x) - hll_err,
                               bracket=[0, 3 * m])

    print(f'\t/* precision {prec} */')
    print(f'\t{int(solution.root)},')
