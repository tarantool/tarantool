## feature/core

* Implemented a timeout for transactions after
  which they are rolled back (gh-6177).
  Implemented new C API function 'box_txn_set_timeout'
  to set timeout for transaction.
