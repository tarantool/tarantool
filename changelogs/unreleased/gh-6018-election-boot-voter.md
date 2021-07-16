## bugfix/replication

* Fixed a cluster sometimes being unable to bootstrap if it contains nodes with
  `election_mode` `manual` or `voter` (gh-6018).
