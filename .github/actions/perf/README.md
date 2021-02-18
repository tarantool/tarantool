# Prepare docker image with benchmarks and run testings using it

Action prepares 2 images:

- perf_master - image with benchmarks installed, changing very rare
- perf_'<commit_sha>' - image with Tanatool installed and benchmarks built with Tarantool headers

## How to use Github Action from Github workflow

Add the following code to the running steps after checkout done:
```
  - name test
    env:
      BENCH: '<sysbench;tpcc;nosqlbench;ycsb>' # anyone benchmark from the list
      ARGS: '<tree;hash>' # anyone argument for the benchmark
      DOCKER_RUN_ARGS: '--memory=3g' # any docker options to run docker on testing
      IMAGE_SUFFIX: '_tpch' # any suffix like is used for TPC-H testing
    uses: ./.github/actions/perf
```

