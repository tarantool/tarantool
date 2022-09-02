## bugfix/core

* Fixed degradation introduced in Tarantool 2.10.2 that could cause log
  messages to be written to data files thus causing data corruption.
  The issue was fixed by reverting the fix for gh-4450.
