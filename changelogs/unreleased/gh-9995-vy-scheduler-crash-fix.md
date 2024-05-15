## bugfix/vinyl

* Fixed a use-after-free bug in the compaction scheduler triggered by a race
  with a concurrent DDL operation (gh-9995).
