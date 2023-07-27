## bugfix/box

* All DDL functions from the `box.schema` module are now wrapped into a
  transaction to avoid database inconsistency on failed operations (gh-4348).
