## bugfix/replication

* Fixed a false-positive split-brain error when an old synchronous transaction
  queue owner confirmed the same transactions which were already confirmed by
  the new queue owner, or rolled back the same transactions which were rolled
  back by the new queue owner (gh-9138).
