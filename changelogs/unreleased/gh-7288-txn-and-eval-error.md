## bugfix/core

* Fixed a bug when the "Transaction is active at return from function" error
  was overwriting expression evaluation errors in case the expression begins a transaction (gh-7288).
