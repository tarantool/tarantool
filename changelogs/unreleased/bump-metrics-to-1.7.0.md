## feature/metrics

* Updated the metrics submodule to 1.7.0.

  Changes in 1.7.0:

  - graphite: Added an ability to send metrics to multiple servers.
  Backward compatibility with previous plugin versions is preserved.
  From now on, the `init` method assigns a unique name to the created fiber
  using the incoming graphite server `opts` (if provided). Added a new `stop()`
  method to stop all fibers started by the plugin ([gh-540][mgh-540]).

  - Deleting a replica `via box.space._cluster:delete()` no longer leaves
  information about that replica in the metrics (it was only removed after
  a cluster restart) ([gh-538][mgh-538]).

[mgh-540]: https://github.com/tarantool/metrics/pull/540
[mgh-538]: https://github.com/tarantool/metrics/pull/538
