## bugfix/replication

* Fixed a newly bootstrapped leader entering `waiting_for_own_rows` and
  remaining read-only because of a connected instance from another
  replicaset (gh-10592).
