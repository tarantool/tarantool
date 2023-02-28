## bugfix/replication

* Fixed a bug when new instances could try to register via an anon instance
  which previously failed to apply `box.cfg{replication_anon = false}`.
