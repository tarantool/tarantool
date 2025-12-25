## bugfix/config

* Fixed a startup failure when a Tarantool 3.x instance configured via
  the declarative config joined a 2.x replica set, where the master had
  no instance or replica set names stored in its snapshot (gh-10426).
