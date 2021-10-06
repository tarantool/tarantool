## core/feature

* Previously, if a yield occurs for a transaction that does not
  support it, we roll back all its statements, but still process
  its new statements (they will roll back with each yield). Also,
  the transaction will be rolled back when a commit is attempted.
  Now we stop processing any new statements right after first yield,
  if transaction doesn't support it.
