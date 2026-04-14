## bugfix/election

* Fixed a bug where a replica could crash or exhibit undefined behavior when a
  new election leader sent this replica a promotion announcement, but the
  replica received it just before performing a cascading journal rollback. This
  could happen if the `wal.queue_max_size` setting was reached.
