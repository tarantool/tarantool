#!/usr/bin/env bash
#
# Runs every workload on every index and prints the results as CSV:
#
#   run.sh <build dir> <data dir> [key count] > results.csv
#
# Takes all the *.keys files of the data dir. If there are none, generates
# uniform.keys with the given number of keys, 1000000 by default.
#
set -eu

if [ $# -lt 2 ]; then
    echo "usage: $0 <build dir> <data dir> [key count]" >&2
    exit 1
fi
gen=$1/perf/learned/learned_gen
bench=$1/perf/learned/learned_bench
data=$2
count=${3:-1000000}
indexes=${INDEXES:-bps alex alex:1048576 alex:16384}
workloads=${WORKLOADS:-lookup lower_bound scan count insert delete
    ycsb_a ycsb_b ycsb_d ycsb_e drift}

mkdir -p "$data"
if ! ls "$data"/*.keys > /dev/null 2>&1; then
    "$gen" keys uniform "$count" 1 "$data/uniform.keys"
fi

echo "dataset,workload,$("$bench" header)"
for keys in "$data"/*.keys; do
    dataset=$(basename "$keys" .keys)
    for workload in $workloads; do
        ops=$data/$dataset.$workload.ops
        "$gen" ops "$keys" "$workload" "$count" 2 "$ops"
        for index in $indexes; do
            echo "$dataset $workload $index" >&2
            # ALEX may hang on some keys, see README.md.
            row=$(timeout 600 "$bench" "$index" "$keys" "$ops") || true
            echo "$dataset,$workload,${row:-$index,,,,,,,,,,,,failed}"
        done
    done
done
