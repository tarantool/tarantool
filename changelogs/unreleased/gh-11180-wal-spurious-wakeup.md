## bugfix/core

* Fixed a crash, undefined behaviour, and inconsistent data across replicas that
  could all occur when the WAL queue was full (`box.cfg.wal_queue_max_size` was
  reached) and transactions blocked on that queue were woken up or cancelled
  spuriously (for example, manually via `fiber:wakeup()` or `fiber:cancel()`).
  (gh-11180).
