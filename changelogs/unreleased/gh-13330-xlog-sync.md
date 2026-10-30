## bugfix/core

* On macOS, request a flush of the storage device write cache when
  synchronizing snapshot, WAL and vinyl files on close, and when calling
  `fio.fsync()` or `fio.fdatasync()` (gh-13330).
