## bugfix/core

* Fixed a crash that happened if the transaction was aborted (for example,
  by fiber yield with MVCC off) while space `on_replace` or `before_replace`
  was running (gh-8027).
