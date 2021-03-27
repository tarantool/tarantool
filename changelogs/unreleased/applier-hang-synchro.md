## bugfix/replication

* Fix applier hang on a replica after it fails to process CONFIRM or ROLLBACK
  message coming from a master.
