## feature/replication

* It is now possible to decode incoming replication data in a separate thread.
  Added the `replication_threads` configuration option that controls how many
  threads may be spawned to do the task (default is 1) (gh-6329).
