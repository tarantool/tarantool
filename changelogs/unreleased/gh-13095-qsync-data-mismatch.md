## bugfix/raft

* Fixed a bug where a synchronous transaction, temporarily blocked because the
  synchronous transaction queue was full (`replication.synchro_queue_max_size`
  was reached), during a leader change or a timeout
  (`replication.synchro_timeout`) could be rolled back, but then appear again
  and even be committed (locally after a restart or on another node) (gh-13095).
