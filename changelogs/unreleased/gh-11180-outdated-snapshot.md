## bugfix/core

* Fixed the creation of broken snapshots, which could contain outdated entries
  also applied in the following xlog files. This could happen if the
  transactions would pile up and fill the whole WAL queue
  (`box.cfg.wal_queue_max_size` was reached), and a snapshot was created at this
  moment (gh-11180).
