## bugfix/core

* Fixed the `BEGIN`, `COMMIT`, and `ROLLBACK` counters in the `box.stat()` output.
  Now they show the number of started, committed, and rolled back transactions
  (gh-7583).
