## bugfix/vinyl

* Fixed a bug when the garbage collector purged run files left after a dropped
  space without waiting for compaction completion. The bug could result in
  a compaction failure with a "No such file or directory" error (gh-11163).
