## bugfix/replication

* Fixed an error when a replica, at attempt to subscribe to a foreign cluster
  (with different replicaset UUID), didn't notice it is not possible, and
  instead was stuck in an infinite retry loop printing an error about "too
  early subscribe" (gh-6094).
