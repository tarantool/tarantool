## bugfix/memtx

* Fixed conflicted transactions in memtx being able to perform read-only
  operations which gave spurious results (gh-7238).
