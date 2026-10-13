## bugfix/replication

* Fixed a false duplicate-name error when registering a named replica again
  after removing it from `_cluster` while its applier was still present
  (gh-12984).
