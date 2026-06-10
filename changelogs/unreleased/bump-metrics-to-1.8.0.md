## feature/metrics

* Updated the metrics submodule to 1.8.0.

  Changes in 1.8.0:

  * Added selector-based filtering for custom metrics. Custom metrics can now
    be associated with hierarchical selectors and controlled via
    `metrics.set_filter()` or `metrics.cfg()` include/exclude options. Unknown
    include/exclude entries are treated as custom selectors, while built-in
    metric group names keep the existing behavior ([gh-543][mgh-543]).

  * Added `metrics.namespace()` and `metrics.set_filter()` to mark custom
    collectors and callbacks with selectors and filter them at collection time
    ([gh-543][mgh-543]).

[mgh-543]: https://github.com/tarantool/metrics/pull/543
