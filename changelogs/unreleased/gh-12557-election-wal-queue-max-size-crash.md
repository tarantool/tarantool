## bugfix/election

* Fixed a bug where a replica could crash or exhibit undefined behavior when
  the election leader sent this replica a synchronous replication control
  command (promotion or demotion), but the replica received it just before
  performing a cascading journal rollback. This could happen if the
  `wal.queue_max_size` setting was reached (gh-12557).
