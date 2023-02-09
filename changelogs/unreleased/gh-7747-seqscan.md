## feature/sql

* Introduced the new keyword `SEQSCAN` for SQL `SELECT` queries. You may now
  use a scanning SQL `SELECT` query without the `SEQSCAN` keyword only if the
  `sql_seq_scan` session setting is set to `true`. The default session setting
  value is controlled by the new `compat` option `sql_seq_scan_default`
  (gh-7747).
