## bugfix/vinyl

* Fixed a heap-use-after-free bug in the Vinyl read iterator caused by a race
  between a disk read and a memory dump task. The bug could lead to a crash or
  an invalid query result (gh-8852).
