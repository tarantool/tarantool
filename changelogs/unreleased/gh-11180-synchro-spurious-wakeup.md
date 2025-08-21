## bugfix/raft

* Fixed inconsistent data which could happen when the synchro queue was full
  (`box.cfg.replication_synchro_queue_max_size` was reached) and the
  transactions blocked on that queue were woken up or cancelled spuriously (for
  example, manually via `fiber:wakeup()` or `fiber:cancel()`). Specifically,
  transactions could get committed in a wrong order or newly joined replicas
  could have data not present on the master (gh-11180).
