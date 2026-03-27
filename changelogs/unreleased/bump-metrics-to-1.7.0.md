## feature/metrics

* Updated the metrics submodule to 1.7.0.

  Changes in 1.7.0:

  - `graphite`: ability to send metrics to the multiple servers.
  Backward compatibility with previous plugin version is preserved.
  From now on `init` method assigns an unique name to the created fiber
  using incoming graphite server `opts` (if passed). Added new `stop()`
  method to stop all fibers started by the plugin ([gh-540][mgh-540]).

  - Deleting a replica via `box.space._cluster:delete()` doesn't delete
  information about this replica from the metrics (it's gone only
  after cluster is restarted) ([gh-538][mgh-538]).

[mgh-540]: https://github.com/tarantool/metrics/pull/540
[mgh-538]: https://github.com/tarantool/metrics/pull/538
