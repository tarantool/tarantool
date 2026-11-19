colorize_diff = False

def ratio(base, value):
    return 100.0 / base * value

def get_diff(base, value, reverse = False, orange_thr = 2):
    result = ratio(base, value) - 100.0
    result *= -1 if reverse else 1
    if result == 0:
        return '-'
    if result < 0:
        color = 'green'
    elif result <= orange_thr:
        color = 'RedOrange'
    else:
        color = 'red'
    result *= -1 if reverse else 1
    prefix = '+' if result > 0 else ''
    if colorize_diff:
        return f'$\\color{"{"}{color}{"}"}{prefix}{result:.02f}％$'
    else:
        return f'{prefix}{result:.02f}%'

def print_table(rows, prefix = 0):
    if len(rows) == 0:
        return
    cols = len(rows[0])
    for row in rows:
        assert(cols == len(row))
    max_cell_len = [ -1 ] * cols
    for row in rows:
        for i in range(len(row)):
            length = len(row[i].encode('utf-16-le')) // 2
            max_cell_len[i] = max((max_cell_len[i], length))
    for i in range(len(rows)):
        print(' ' * prefix, end = '')
        print('|', end = '')
        for j in range(len(rows[i])):
            print(f' {rows[i][j].ljust(max_cell_len[j])} |', end = '')
        print('') # Newline.
        if i == 0:
            print(' ' * prefix, end = '')
            print('|', end = '')
            for j in range(len(rows[i])):
                print(f' {"-" * (max_cell_len[j])} |', end = '')
            print('')

    print('')

from statistics import mean, median, stdev, variance
from math import sqrt
import sys

def t_test(a_count, b_count, a_mean, b_mean, a_variance, b_variance, p = 0.002):
    # Sources of values:
    # - https://en.wikipedia.org/wiki/Student%27s_t-distribution
    # - https://www.itl.nist.gov/div898/handbook/eda/section3/eda3672.htm
    distribution = {}
    distribution[0.002] = [
        318.309, 22.327, 10.215, 7.173, 5.893, 5.208, 4.785, 4.501, 4.297, 4.144, # df = 1..10.
        4.025, 3.930, 3.852, 3.787, 3.733, 3.686, 3.646, 3.610, 3.579, 3.552,     # df = 11..20.
        3.527, 3.505, 3.485, 3.467, 3.450, 3.435, 3.421, 3.408, 3.396, 3.385,     # df = 21..30.
        3.375, 3.365, 3.356, 3.348, 3.340, 3.333, 3.326, 3.319, 3.313, 3.307,     # df = 31..40.
        3.301, 3.296, 3.291, 3.286, 3.281, 3.277, 3.273, 3.269, 3.265, 3.261,     # df = 41..50.
        3.258, 3.255, 3.251, 3.248, 3.245, 3.242, 3.239, 3.237, 3.234, 3.232,     # df = 51..60.
        3.229, 3.227, 3.225, 3.223, 3.220, 3.218, 3.216, 3.214, 3.213, 3.211,     # df = 61..70.
        3.209, 3.207, 3.206, 3.204, 3.202, 3.201, 3.199, 3.198, 3.197, 3.195,     # df = 71..80.
        3.194, 3.193, 3.191, 3.190, 3.189, 3.188, 3.187, 3.185, 3.184, 3.183,     # df = 81..90.
        3.182, 3.181, 3.180, 3.179, 3.178, 3.177, 3.176, 3.175, 3.175, 3.174,     # df = 91..100.
        3.090,                                                                    # df = ∞.
    ]

    volatility_coefficient = 0.25 # Tuning for the execution environment.

    mean_difference = abs(a_mean - b_mean)
    standard_error = sqrt(a_variance / a_count + b_variance / b_count)
    t_value = mean_difference / standard_error
    t_value *= volatility_coefficient

    assert(a_count > 1 and b_count > 1)
    df = pow(a_variance / a_count + b_variance / b_count, 2) / (
             (1 / (a_count - 1)) * pow(a_variance / a_count, 2) +
             (1 / (b_count - 1)) * pow(b_variance / b_count, 2)
         )
    t_value_critical = distribution[p][min(int(df) - 1, len(distribution[p]) - 1)]

    if t_value > t_value_critical:
        if a_mean > b_mean:
            result = 'Regression' if t_value > t_value_critical * 1.5 else 'Suspecious'
        else:
            result = 'Improvement' if t_value > t_value_critical * 1.5 else 'Uncertain'
        return f"{result} (t-value {t_value:.2f} > {t_value_critical:.2f})"
    else:
        return "~"

def get_stats(fname):
    s = open(fname).read()

    test_results = {}

    for l in s.splitlines():
        if len(l) == 0:
            continue
        name, result, _ = l.split()
        if name not in test_results:
            test_results[name] = []
        test_results[name].append(float(result))

    test_stats = {}

    for name, results in test_results.items():
        test_stats[name] = {}
        test_stats[name]['count'] = len(results)
        test_stats[name]['min'] = min(results)
        test_stats[name]['max'] = max(results)
        test_stats[name]['mean'] = mean(results)
        test_stats[name]['median'] = median(results)
        test_stats[name]['variance'] = variance(results)

        sorted_data = sorted(results)
        test_stats[name]['90%'] = sorted_data[int(len(sorted_data) * 0.90)]
        test_stats[name]['95%'] = sorted_data[int(len(sorted_data) * 0.95)]
        test_stats[name]['99%'] = sorted_data[int(len(sorted_data) * 0.99)]

        min_ = test_stats[name]['min']
        avg_ = test_stats[name]['mean']
        disp_ = avg_ - min_;
        test_stats[name]['disp%'] = 100.0 / avg_ * disp_
        test_stats[name]['stdev%'] = 100.0 / avg_ * stdev(results)

    return test_stats

test_stats_old = get_stats(sys.argv[1])
test_stats_new = get_stats(sys.argv[2]) if len(sys.argv) > 2 else test_stats_old

table = [['test', '50%', 'disp', 'stdev', 't-test']]
for name in test_stats_old:
    assert(name in test_stats_old)
    assert(name in test_stats_new)

    stats_old = test_stats_old[name]
    stats_new = test_stats_new[name]

    def stat(name):
        old = '{:.2f}'.format(stats_old[name])
        new = '{:.2f}'.format(stats_new[name])
        diff = get_diff(stats_old[name], stats_new[name], reverse = True)
        return old, new, diff

    def merge(old, new, diff):
        return f'{old} / {new} ({diff})'

    t_mean_old, t_mean_new, t_mean_diff = stat('mean')
    t_median_old, t_median_new, t_median_diff = stat('median')
    t_p90_old, t_p90_new, t_p90_diff = stat('90%')
    t_p95_old, t_p95_new, t_p95_diff = stat('95%')
    t_p99_old, t_p99_new, t_p99_diff = stat('99%')

    t_disp = '{:.2f}% / {:.2f}%'.format(stats_old['disp%'], stats_new['disp%'])
    t_stdev = '{:.2f}% / {:.2f}%'.format(stats_old['stdev%'], stats_new['stdev%'])

    t_mean = merge(t_mean_old, t_mean_new, t_mean_diff)
    t_median = merge(t_median_old, t_median_new, t_median_diff)

    t_test_result = t_test(stats_old['count'], stats_new['count'],
                           stats_old['mean'], stats_new['mean'],
                           stats_old['variance'], stats_new['variance'])

    table.append([name, t_median, t_disp, t_stdev, t_test_result])
    name = ''

print_table(table)
