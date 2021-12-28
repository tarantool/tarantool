## bugfix/replication

* Fixed potential obsolete data write in synchronious replication
  due to race in accessing terms while disk write operation is in
  progress and not yet completed.
