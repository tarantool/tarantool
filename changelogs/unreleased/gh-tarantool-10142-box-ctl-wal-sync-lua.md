## feature/core

* Added the `box.ctl.wal_sync()` function, which waits until
  all submitted writes are successfully flushed to the disk.
  Throws an error if a write fails. After the function is
  executed one may reliably use box.info.vclock for comparisons
  when choosing a new master (gh-10142).
