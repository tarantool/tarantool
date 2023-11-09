## bugfix/replication

* Now transactions are not rolled back if the transaction fiber is
  cancelled when waiting for quorum from replicas (gh-9480).
