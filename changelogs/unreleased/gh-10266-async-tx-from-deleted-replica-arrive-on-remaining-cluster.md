## bugfix/replication

* Fixed a bug that allowed asynchronous transactions from a replica deleted from
  the cluster to arrive on the remaining cluster members (gh-10266).
