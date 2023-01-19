## bugfix/replication

* Fixed local space writes failing with error "Found uncommitted sync
  transactions from other instance with id 1" when synchronous transaction queue
  belongs to another instance and isn't empty (gh-7592).
