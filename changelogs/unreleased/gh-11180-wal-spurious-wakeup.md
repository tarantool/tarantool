## bugfix/core

* Fixed a crash / undefined behaviour / inconsistent data which could all happen
  when the WAL queue was full (`box.cfg.wal_queue_max_size` was reached) and the
  txns blocked on that queue were woken up or cancelled spuriously (for example,
  manually via `fiber:wakeup()` or `fiber:cancel()`) (gh-11180).
