## bugfix/core

* Fixed a bug introduced in Tarantool 2.10.2: log messages
  could be written to data files thus causing data corruption.
  The issue was fixed by reverting the fix for gh-4450.
