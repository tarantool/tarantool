## bugfix/replication

* Fixed an assertion failure in debug builds when a replica was removed from
  `_cluster` while its SUBSCRIBE request was waiting for WAL (gh-12984).
