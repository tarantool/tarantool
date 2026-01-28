## bugfix/box

* Fixed a bug where `.xlog.inprogress` files were not automatically deleted
  during server startup if the `wal_dir` value was not the default (gh-12081).
