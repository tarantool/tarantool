# The script accepts a benchmark config in JSON format that
# contains a list with benchmark name and it's subtests in the
# following format:
#
# [
#   {
#     "subtests": [
#       "sum_first",
#       "sum_last"
#     ],
#     "bench_name": "column_scan"
#   },
#   ...
# ]
#
# it traverses test configs and extracts performance results for
# each test from an InfluxDB to CSV files in the following format:
#
# time,commit,metric1,metric2
# 2025.01.01 3:00:00 +0100,aaa0,154023,10.43
# 2025.01.02 3:00:00 +0100,aaa1,138455,10.23
#
# and generate a YAML config in the following format:
#
# tests:
#   luafun:
#     type: csv
#     file: examples/csv/data/luafun.csv
#     time_column: time
#     metrics: [drop_while]
#     csv_options:
#       delimiter: ','
#       quote_char: "'"
#
# The data in CSV files and YAML config are suitable for using
# with by Apache Otava [1], that performs statistical analysis of
# performance test results and finds change-points and notifies
# about possible performance regressions.
#
# 1. Apache Otava, https://github.com/apache/otava
# 2. InfluxDB 2.0 python client, https://influxdb-client.readthedocs.io/
# 3. InfluxDB API, https://docs.influxdata.com/influxdb/v2/api-guide/api_intro/
#
# Requirements: `pip install influxdb-client pyyaml`.

from dateutil import parser
from influxdb_client import InfluxDBClient, Dialect
import argparse
import copy
import csv
import json
import os
import yaml

INFLUXDB_QUERY_TEMPLATE = """
from(bucket:"{influx_bucket}")
|> range(start: -{hours}h, stop: now())
|> filter(fn: (r) => r._measurement == "{bench_name}")
|> filter(fn: (r) => r.branch == "{branch}")
|> filter(fn: (r) => r.name == "{subtest_name}")
|> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
|> drop(columns: ["_start", "_stop", "iterations"])
"""

DEFAULT_BASE_PATH = os.path.abspath(os.getcwd())
DEFAULT_HOURS_NUM = 24*30
DEFAULT_BRANCH_NAME = "master"
DEFAULT_INFLUXDB_BUCKET = "perf-debug"
DEFAULT_INFLUXDB_ORG = "tarantool"
DEFAULT_OTAVA_CONFIG = "otava.yaml"
DEFAULT_BENCHMARK_CONFIG = "config_bench.json"


def escape_string(string, escape_chs=["/", "<", ">", ":"]):
    escaped_str = string
    for ch in escape_chs:
        escaped_str = escaped_str.replace(ch, "_")
    return escaped_str


def influx_download_stat(
        client, bucket, branch, bench_name, subtest_name, hours):

    query = INFLUXDB_QUERY_TEMPLATE.format(
        influx_bucket=bucket, hours=hours, branch=branch,
        bench_name=bench_name, subtest_name=subtest_name)
    return client.query_api().query_csv(
        query, dialect=Dialect(header=True, annotations=[]))


def create_csv_report(csv_iterator, csv_path):
    csv_file = open(csv_path, "w", newline="")
    writer = csv.writer(csv_file, dialect="excel", delimiter=",",
                        quotechar="'", quoting=csv.QUOTE_MINIMAL)
    header = next(csv_iterator)
    # The method `.index` raises, when field `_time` is not found.
    time_idx = header.index("_time")
    writer.writerow(header)
    for row in csv_iterator:
        # Make datetime format copatible with Apache Otava.
        row[time_idx] = \
            parser.parse(row[time_idx]).strftime("%Y-%m-%d %H:%M:%S %z")
        writer.writerow(row)
    csv_file.close()


def main():
    parser = argparse.ArgumentParser(
        prog="extract_perf_data", description="Build data for Apache Otava.")

    parser.add_argument("--influx_url", required=True)
    parser.add_argument("--influx_token", required=True)
    parser.add_argument("--influx_org", default=DEFAULT_INFLUXDB_ORG)
    parser.add_argument("--influx_bucket", default=DEFAULT_INFLUXDB_BUCKET)
    parser.add_argument("--branch_name", default=DEFAULT_BRANCH_NAME)
    parser.add_argument("--config", default=DEFAULT_BENCHMARK_CONFIG)
    parser.add_argument("--hours", default=DEFAULT_HOURS_NUM)
    parser.add_argument("--base_path", default=DEFAULT_BASE_PATH)
    args = parser.parse_args()

    print(("""Used parameters:
    Git branch name: {}
    Number of hours: {}
    Benchmarks config filename: {}
    """).format(args.branch_name, args.hours, args.config))

    client = InfluxDBClient(
        url=args.influx_url,
        token=args.influx_token,
        org=args.influx_org
    )

    default_test_config = {
        "inherit": ["common"],
    }
    otava_tests = {}
    # Format: {'metrics': ['uri.escape', 'uri.unescape'], \
    #   'bench_name': 'uri_escape_unescape'}.
    with open(args.config) as f:
        bench_configs = json.load(f)
        for test_config in bench_configs:
            bench_name = test_config["bench_name"]
            for subtest_name in test_config["subtests"]:
                escaped_subtest_name = escape_string(subtest_name)
                test_subtest_name = "{bench_name}_{subtest_name}".format(
                    bench_name=bench_name, subtest_name=escaped_subtest_name)
                csv_filename = "{}.csv".format(test_subtest_name)
                csv_path = os.path.join(
                    os.path.abspath(args.base_path), csv_filename)
                print("Extract data {} ({}): {}".format(
                    bench_name, subtest_name, csv_path))
                csv_iterator = influx_download_stat(
                    client, args.influx_bucket, args.branch_name, bench_name,
                    subtest_name, args.hours)
                create_csv_report(csv_iterator, csv_path)

                # Build a test config for Apache Otava configuration.
                otava_test_config = copy.deepcopy(default_test_config)
                otava_test_config["file"] = csv_path
                otava_tests[test_subtest_name] = otava_test_config

    # Close an InfluxDB client.
    client.close()

    # CPU time (`cpu_time`) and real time (`real_time`) are not
    # processed by Apache Otava.
    otava_config = {
        "templates": {
            "common": {
                "csv_options": {
                    "delimiter": ",",
                },
                "time_column": "_time",
                "attributes": ["commit"],
                "type": "csv",
                "metrics": {
                    "items_per_second": {
                        "direction": 1
                    },
                }
            },
        },
        "tests": otava_tests,
        "test_groups": {
            "tarantool": list(otava_tests.keys()),
        }
    }
    with open(DEFAULT_OTAVA_CONFIG, "w") as config:
        yaml.dump(otava_config, config, default_flow_style=False)


if __name__ == "__main__":
    main()
