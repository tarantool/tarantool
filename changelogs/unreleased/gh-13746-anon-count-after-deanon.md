## bugfix/replication

* Fixed `box.info.replication_anon` not being updated when an anonymous
  replica was deanonymized: the count stayed non-zero while the callable
  removed the replica after it had been registered (gh-13746).
