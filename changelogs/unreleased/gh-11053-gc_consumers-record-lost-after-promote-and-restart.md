## bugfix/box

* Fixed a bug when a record could be lost from the `_gc_consumers` space after
  receiving a promote request from another instance and restarting the current
  instance (gh-11053).
