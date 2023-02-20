## bugfix/core

* Fixed a crash that happened if a transaction was aborted (for example,
  by fiber yield with MVCC off) while the space's `on_replace` or
  `before_replace` trigger was running (gh-8027).
