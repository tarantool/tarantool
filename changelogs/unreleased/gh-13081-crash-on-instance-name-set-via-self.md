## bugfix/replication

* Fixed a crash when calling `box.cfg{instance_name = ...}` on a
  read-only replica whose `box.cfg.replication` includes its own URI
  and the real master is not reachable.
