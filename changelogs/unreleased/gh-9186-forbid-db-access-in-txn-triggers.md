## bugfix/box

* An attempt to execute a DDL, DML, DQL or DCL query within
  a transactional trigger (`on_commit` or `on_rollback`) now fails
  with a specific error (gh-9186).
