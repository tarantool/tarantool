## feature/metrics

* Updated the metrics submodule to 1.6.1.

  Changes in 1.6.1:

  - Added a new `schema` metrics category with the `schema_needs_upgrade` metric, enabled by default
    ([gh-524][mgh-524]) ([gh-529][mgh-529]).

  Changes in 1.6.0:

  - Added a new `memory` metric, enabled by default
    ([gh-519][mgh-519]).

  - Added a new `memory_virt` metric, enabled by default
    ([gh-521][mgh-521]).

  - Fixed possible `fio.read` errors
    ([gh-527][mgh-527]).

  Changes in 1.5.0:

  - Added `tnt_cartridge_config_checksum` metric
    ([gh-516][mgh-516]).

  Changes in 1.4.0:

  - New optional ``label_keys`` parameter for ``counter()`` and ``gauge()`` metrics
    ([gh-508][mgh-508]).

[mgh-508]: https://github.com/tarantool/metrics/pull/508
[mgh-516]: https://github.com/tarantool/metrics/pull/516
[mgh-519]: https://github.com/tarantool/metrics/pull/519
[mgh-521]: https://github.com/tarantool/metrics/pull/521
[mgh-524]: https://github.com/tarantool/metrics/pull/524
[mgh-527]: https://github.com/tarantool/metrics/pull/527
[mgh-529]: https://github.com/tarantool/metrics/pull/529
