## bugfix/replication

* Fixed a replica sending its master an outdated acknowledgement right after
  reconnecting, which made the replica's vclock progress appear to move
  backwards on the master in `box.info.replication` (gh-12795).
