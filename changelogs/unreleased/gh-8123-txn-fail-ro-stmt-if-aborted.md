## bugfix/core

* Fixed a bug because of which read-only statements executed in a transaction
  aborted by yield or timeout completed successfully. Now, read-only statements
  fail in this case, just like write statements (gh-8123).
