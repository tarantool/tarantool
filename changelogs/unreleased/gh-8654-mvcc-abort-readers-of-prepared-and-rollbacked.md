## bugfix/core

* Now MVCC engine automatically aborts a transaction if it reads changes
  of a prepared transaction and this transaction is aborted (gh-8654).
