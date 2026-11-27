## bugfix/replication

* Fixed a crash or an inconsistent replica registration when inserting an
  already registered replica UUID into `_cluster` under another ID using
  different letter case (gh-13846).
