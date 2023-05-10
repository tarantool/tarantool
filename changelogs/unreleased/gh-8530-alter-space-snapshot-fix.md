## bugfix/core

* Fixed a bug because of which a dirty (not committed to WAL) DDL record could
  be written to a snapshot and cause a recovery failure (gh-8530).
