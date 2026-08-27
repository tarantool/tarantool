## bugfix/raft

* Fixed a bug when a synchronous transaction, temporarily blocked on the
  synchronous transaction queue being full (`replication.synchro_queue_max_size`
  reached), during a leader change or a timeout (`replication.synchro_timeout`)
  could be rolled back, but then appear again and even get committed (locally
  after a restart or on another node) (gh-13095).
