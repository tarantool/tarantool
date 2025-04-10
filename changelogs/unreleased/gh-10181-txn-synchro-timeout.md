## bugfix/replication

* Fixed a bug when with the new `replication_synchro_timeout` behavior
  (`compat.replication_synchro_timeout = 'new'`) a user fiber could hang
  indefinitely if for some reason the quorum could not respond with an ACK
  to a synchronous transaction.
  A new configuration option `txn_synchro_timeout` has been introduced
  for this purpose.
