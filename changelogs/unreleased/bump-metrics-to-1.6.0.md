## feature/metrics

* Updated the metrics submodule to 1.6.0.

  - Added a new `memory` metric, enabled by default
    ([gh-519][mgh-519]).

  - Added a new `memory_virt` metric, enabled by default
    ([gh-521][mgh-521]).

  - Added a new `schema` metrics category with `schema_needs_upgrade` metric, enabled by default
    ([gh-524][mgh-524]).

  - Bumped tarantool and cartridge versions in tests
    ([gh-525][mgh-525]).

  - Fixed possible `fio.read` errors
    ([gh-527][mgh-527]).

[mgh-519]: https://github.com/tarantool/metrics/pull/519
[mgh-521]: https://github.com/tarantool/metrics/pull/521
[mgh-524]: https://github.com/tarantool/metrics/pull/524
[mgh-525]: https://github.com/tarantool/metrics/pull/525
[mgh-527]: https://github.com/tarantool/metrics/pull/527
