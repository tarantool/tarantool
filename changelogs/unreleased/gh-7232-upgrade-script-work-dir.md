## bugfix/core

* Fixed Tarantool not being able to recover from old snapshots when
  `box.cfg.work_dir` and `box.cfg.memtx_dir` were both set (gh-7232).
