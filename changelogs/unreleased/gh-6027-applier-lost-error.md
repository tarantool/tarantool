## bugfix/replication

* When an error happened during appliance of a transaction received from a
  remote instance via replication, it was always reported as "Failed to write
  to disk" regardless of what really happened. Now the correct error is shown.
  For example, "Out of memory", or "Transaction has been aborted by conflict",
  and so on (gh-6027).
