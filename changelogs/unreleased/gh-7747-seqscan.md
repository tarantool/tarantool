## feature/sql

* Introduced a new `SEQSCAN` keyword for usage in scanning `SELECT` queries.
  A new session setting `sql_seq_scan` can be used to allow or restrict scanning
  queries without `SEQSCAN` (gh-7747).
