## bugfix/core

* Fixed the node writing an empty `00000000000000000000.xlog` file regardless of
  the actual vclock when interrupted during the initial `box.cfg()` call
  (gh-8704).
