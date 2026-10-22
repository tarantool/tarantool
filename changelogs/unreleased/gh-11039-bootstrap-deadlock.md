## bugfix/replication

* Fixed a bootstrap deadlock when registering a new replica raises the
  synchronous replication quorum and its initial data contains a
  transaction awaiting confirmation (gh-11039).
