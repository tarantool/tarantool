## bugfix/replication

* Fix recovery of a rolled back multi-statement synchronous transaction which
  could lead to the transaction being applied partially, and to recovery errors.
  It happened in case the transaction worked with non-sync spaces (gh-5874).
