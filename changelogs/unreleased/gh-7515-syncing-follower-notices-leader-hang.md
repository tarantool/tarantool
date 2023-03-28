## bugfix/replication

* Fixed an issue when the nodes synchronizing with
  a hung leader reported the leader as alive.
  This behavior led to the delay of the new elections (gh-7515).
