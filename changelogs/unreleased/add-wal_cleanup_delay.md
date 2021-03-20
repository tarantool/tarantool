## bugfix/core

* Introduce `wal_cleanup_delay` option to prevent early cleanup
  of `*.xlog` files which are needed by replicas and lead to
  `XlogGapError` (gh-5806).
