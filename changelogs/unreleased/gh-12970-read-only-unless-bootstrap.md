## feature/core

* Added a new `box.cfg.read_only` value: `'unless_bootstrap'`. An instance
  configured this way becomes writable only if it has bootstrapped the
  replicaset on the current startup and stays read-only if it has recovered
  from a local snapshot or has joined an already bootstrapped replicaset. The
  mode is resolved atomically during the initial `box.cfg()` call, so the
  instance never goes through a transient writable state. The value may
  be set only during the initial `box.cfg()` call (gh-12970).
