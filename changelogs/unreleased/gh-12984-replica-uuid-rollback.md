## bugfix/replication

* Fixed a named replica disappearing from `box.info.replication` after rolling
  back a UUID change in `_cluster`. In debug builds, the rollback could cause
  an assertion failure instead (gh-12984).
