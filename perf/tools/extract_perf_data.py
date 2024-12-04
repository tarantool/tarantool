# The script extracts performance results to CSV files in the
# following format:
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
# The results in CSV files and YAML config are suitable for using
# with by Apache Otava [1], that performs statistical analysis of
# performance test results and finds change-points and notifies
# about possible performance regressions.
#
# 1. https://github.com/apache/otava
# 2. InfluxDB 2.0 python client,
#    https://influxdb-client.readthedocs.io/
# 3. InfluxDB API,
#    https://docs.influxdata.com/influxdb/v2/api-guide/api_intro/
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
|> filter(fn: (r) => r.name == "{metric_name}")
|> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
|> drop(columns: ["_start", "_stop", "iterations"])
"""

DEFAULT_BASE_PATH = "examples/csv/data"
DEFAULT_HOURS_NUM = 24*90
DEFAULT_BRANCH_NAME = "master"
DEFAULT_INFLUXDB_BUCKET = "perf-debug"
DEFAULT_INFLUXDB_ORG = "tarantool"


def escape_string(string, escape_chs = ["/", "<", ">", ":"]):
    escaped_str = string
    for ch in escape_chs:
        escaped_str = escaped_str.replace(ch, "_")
    return escaped_str


def create_csv_report(client, bucket, branch, bench_name, metric_name, hours, csv_filename):
    query = INFLUXDB_QUERY_TEMPLATE.format(influx_bucket = bucket,
        hours = hours, branch = branch, bench_name = bench_name,
        metric_name = metric_name)
    csv_iterator = client.query_api().query_csv(query, dialect=Dialect(header=True, annotations=[]))
    csv_file = open(csv_filename, "w", newline='')
    writer = csv.writer(csv_file, dialect="excel", delimiter=",",
                        quotechar="'", quoting=csv.QUOTE_MINIMAL)
    for row in csv_iterator:
         if not row[3] == "_time":
            row[3] = parser.parse(row[3]).strftime("%Y-%m-%d %H:%M:%S %z")
         writer.writerow(row)
    csv_file.close()


def main():
    parser = argparse.ArgumentParser(prog='extract_perf_data',
    description = "Extract performance results data and build YAML config.")

    parser.add_argument('--influx_url', required=True)
    parser.add_argument('--influx_token', required=True)
    parser.add_argument('--influx_org', default=DEFAULT_INFLUXDB_ORG)
    parser.add_argument('--influx_bucket', default=DEFAULT_INFLUXDB_BUCKET)
    parser.add_argument('--branch_name', default=DEFAULT_BRANCH_NAME)
    parser.add_argument('--config', default="config_bench.json")
    parser.add_argument('--hours', default=DEFAULT_HOURS_NUM)
    args = parser.parse_args()

    # TODO: Validate arguments.

    print(("""Used parameters:
    Git branch name: {}
    Number of hours: {}
    Benchmarks config filename: {}
    """).format(args.branch_name, args.hours, args.config))

    client = InfluxDBClient(
        url = args.influx_url,
        token = args.influx_token,
        org = args.influx_org
    )

    # Format: {'metrics': ['uri.escape', 'uri.unescape'], 'bench_name': 'uri_escape_unescape'}.
    with open(args.config) as f:
        bench_configs = json.load(f)
        default_test_config = {
            "type": "csv",
            "time_column": "_time",
            "metrics": [],
            "csv_options": {
                "delimiter": ',',
                "quote_char": "'",
            },
        }
        tests = {}
        for config in bench_configs:
            bench_name = config["bench_name"]
            for metric in config["metrics"]:
                escaped_metric = escape_string(metric)
                csv_filename = "{bench_name}_{metric_name}.csv".format(
                            bench_name = bench_name, metric_name = escaped_metric)
                csv_path = os.path.join(DEFAULT_BASE_PATH, csv_filename)
                print("Build report {}: {}, {}".format(bench_name, metric, csv_path))
                create_csv_report(client, args.influx_bucket, args.branch_name, bench_name,
                    metric, args.hours, csv_path)

                # Build benchmark config for Otava configuration file.
                test_config = copy.deepcopy(default_test_config)
                test_config["file"] = csv_filename
                test_config["metrics"] = ["cpu_time", "items_per_second", "real_time"]
                tests[bench_name] = test_config

        otava_config = {}
        otava_config["tests"] = tests

        with open("otava.yaml", 'w' ) as config:
            yaml.dump(otava_config, config, default_flow_style=False)

    # Close a client.
    client.close()


if __name__ == "__main__":
    main()
