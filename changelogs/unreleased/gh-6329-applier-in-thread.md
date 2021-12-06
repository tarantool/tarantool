## feature/replication

* Make it possible to decode incoming replication data in a separate thread. Add
  the `replication_threads` configuration option, which controls how many
  threads may be spawned to do the task (default is 1) (gh-6329).
