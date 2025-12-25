## bugfix/config

* Fixed a startup failure when a Tarantool 3.x instance configured via
  the declarative config joins a 2.x replicaset, where the master has
  no instance or replicaset names stored in its snapshot (gh-10426).
