## bugfix/vinyl

* In some cases vinyl UPSERT deferred field constraint checks until a
  read or compaction. A violating upsert could then be skipped without
  returning an error to the client. Constraints are now validated at
  UPSERT time when update operations may modify constrained columns,
  matching Memtx behavior (gh-12404).
