## feature/core

* **[Breaking change]** Conflicted transactions now throw the
  `Transaction has been aborted by conflict` error on any CRUD operations
  until they are either rolled back (which will return no error) or committed
  (which will return the same error) (gh-7240).

* Read-view transactions now become conflicted on attempts to perform DML
  statements immediately. Previously, this was detected only on the transaction
  preparation stage, that is, when calling `box.commit` (gh-7240).
