## feature/core

* **[Breaking change]** Made conflicted transactions unconditionally
  throw `Transaction has been aborted by conflict` error on any CRUD operations
  until they are either rolled back (which will return no error) or committed
  (which will return the same error) (gh-7240).

* Made read-view transactions become conflicted on attempt to perform DML
  statements immediately: previously this was detected only on transaction
  preparation stage, i.e., when calling `box.commit` (gh-7240).
