## feature/core:

* Now `O_DSYNC` flag is used instead of `O_SYNC` with `wal_mode = fsync` configuration
  This makes WAL operations in fsync mode slightly faster
  Note: WAL files may have outdated access/modification time in metadata
  Restores behavior that was accidentally removed in commit caae99e
