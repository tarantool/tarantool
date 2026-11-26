# Learned index stand

Compares the ALEX learned index (`third_party/alex`) with `bps_tree` set up
like the memtx TREE index. Both get the same keys and the same operations, no
other Tarantool code runs.

## Run

Use a release build:

```sh
cmake --build build-release --target learned_gen learned_bench
bash perf/learned/run.sh build-release ~/learned-data > results.csv
```

`run.sh` takes every `*.keys` file of the data directory, or generates 1M
uniform keys if there are none. `INDEXES="bps alex" run.sh ...` limits the
indexes, `WORKLOADS="lookup drift"` the workloads. A single run:

```sh
learned_gen keys uniform 1000000 1 uniform.keys
learned_gen ops uniform.keys lookup 1000000 2 lookup.ops
learned_bench header
learned_bench bps uniform.keys lookup.ops
learned_bench alex:16384 uniform.keys lookup.ops
```

To run all the synthetic keys, generate them before `run.sh`:

```sh
mkdir -p ~/learned-data
for dist in uniform seq lognormal zipf timestamps clustered steps snowflake \
            nanotime; do
    learned_gen keys $dist 1000000 1 ~/learned-data/$dist.keys
done
```

## Files

| File | What it is |
|---|---|
| `learned.h` | Formats of the key and operation files |
| `gen.cc` | `learned_gen`, the command line of the generator |
| `gen_keys.cc` | Key files: the synthetic keys and the import of real ones |
| `gen_ops.cc` | Operation files: the workloads |
| `gen.h` | Functions `gen.cc` calls from the two files above |
| `rng.h` | Seeded random numbers, the same arguments give the same file |
| `bench.cc` | Runs one operation file on one index, prints a CSV row |
| `bps_index.h` | `bps_tree` with hints, the settings of `memtx_tree.cc` |
| `alex_index.h` | ALEX with unique `uint64` keys |
| `map_index.h` | `std::map`, the reference for the verification |
| `run.sh` | All the workloads on all the indexes |

The payload of an index is a pointer to a 32-byte record, a stand-in for a
tuple. The records lie in memory in random order.

## Synthetic keys

| Name | Keys |
|---|---|
| `uniform` | random below 2^50 |
| `seq` | 1000000, 1000001, ..., like auto-increment |
| `lognormal` | the distribution of the ALEX paper |
| `zipf` | most gaps are tiny, a few are huge |
| `timestamps` | microseconds, bursts of about 1000 events and pauses |
| `clustered` | 64 dense groups at random places |
| `steps` | runs of 64 consecutive keys with jumps of random size |
| `snowflake` | time, worker and sequence number in one 2^60 number |
| `nanotime` | nanoseconds, about 100 between events, 2^60.6 |

`learned_gen import` reads real keys in the SOSD format.

## Workloads

| Name | Loaded keys | Operations |
|---|---|---|
| `lookup` | all | lookups of random present keys |
| `lower_bound` | random half | lower bounds of random keys of the file |
| `scan` | all | lower bound and 99 next records |
| `count` | all | number of keys in a range of 100 keys |
| `insert` | random half | inserts of the other half in random order |
| `delete` | all | deletes of a random half |
| `ycsb_a` | random half | 50% lookups of hot keys, 50% inserts |
| `ycsb_b` | random half | 95% lookups of hot keys, 5% inserts |
| `ycsb_d` | random half | 95% lookups of the newest keys, 5% inserts |
| `ycsb_e` | random half | 95% scans of 1-100 records, 5% inserts |
| `drift` | lower half | 50% lookups, 50% inserts, all above the loaded keys |

The `ycsb_*` mixes follow YCSB: the popularity of the keys is Zipf with 0.99,
the hot keys are random ones. YCSB A and B update records, here they insert,
because an update does not change the index. The inserts go in random order,
in `drift` too, so `drift` is not an append.

Bulk load is measured in every run.

## Columns

| Name | Meaning |
|---|---|
| `index` | `bps`, `alex` or `alex:<max node size in bytes>` |
| `init_keys`, `ops` | number of loaded keys and of operations |
| `build_ns_per_key` | bulk load time |
| `mops` | million operations per second, without timers inside |
| `p50_ns` ... `max_ns` | latency of single operations, with the timer cost |
| `timer_ns` | cost of the timer |
| `bytes_per_key_loaded` | index memory after the bulk load |
| `bytes_per_key_final` | index memory after the operations |
| `verified` | whether all the results match `std::map` |

There are 5 passes for the throughput and 5 for the latency. The reported time
is the minimum of the passes, for the latency it is taken per operation.

## Fairness and limitations

- `get` and `lower_bound` read the key of the found record for every index,
  like a user reads the tuple. `scan` does not read the records.
- A real tuple comparison also decodes msgpack, so `bps` is cheaper here than
  in memtx when it has to read the records: on equal hints.
- `count` is O(log n) in `bps` and O(range) in ALEX, which has no subtree
  sizes.
- ALEX memory is what the library accounts. malloc adds nearly nothing with the
  default node size and up to 20% with 16 KB nodes, because they are many.
- ALEX is built without `-mpopcnt -mlzcnt -mbmi` like the rest of Tarantool,
  see `third_party/alex/README.md`. With them 1M lookups get about 10% faster,
  scans 30%, counts 40%.
- ALEX models use `double`. On consecutive keys with large values it returns
  wrong results or hangs. `verified` is `no` for the former, for the latter
  `run.sh` kills the run after 10 minutes and prints `failed`.
- Synthetic keys say nothing about real data, `uniform` and `seq` are easy for
  ALEX.
- Key `UINT64_MAX` is a sentinel of ALEX and is rejected.
