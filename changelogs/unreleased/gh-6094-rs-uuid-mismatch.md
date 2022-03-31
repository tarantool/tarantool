## bugfix/replication

* Fixed an error when a replica, in an attempt to subscribe to a foreign
  cluster (with different replica set UUID), didn't notice it is not possible,
  and instead was stuck in an infinite retry loop printing an error about "too
  early subscribe" (gh-6094).
