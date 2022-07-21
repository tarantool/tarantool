## bugfix/memtx

* Fixed false transaction conflict on repeatable `insert`/`upsert`
  with the memtx transaction manager enabled (gh-7217).
