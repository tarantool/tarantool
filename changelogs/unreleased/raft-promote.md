## bugfix/replication

* Fix a bug in synchronous replication when rolled back transactions could
  reappear once a sufficiently old instance reconnected (gh-5445).
