## bugfix/replication

* Fixed treating a malformed replication ballot as evidence of an existing
  replicaset, which could lower the connection quorum and allow bootstrap
  without reaching a majority of peers (gh-13646).
