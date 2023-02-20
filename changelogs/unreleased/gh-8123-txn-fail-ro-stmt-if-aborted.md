## bugfix/core

* Fixed read-only statements executing successfully in transactions
  that were aborted by yield or timeout. Now, read-only statements fail in this
  case, just like write statements (gh-8123).
