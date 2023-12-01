## bugfix/core

* Fixed a bug when the assertion in `box_promote_qsync` would fail in the
  debug build mode. The assertion is that at the moment when `box_promote_qsync`
  is called, no other promote is being executed. It turned out that this
  assertion is basically incorrect. In the release build mode, this incorrect
  assumption could potentially lead to writing 2 PROMOTE entries in the same
  term (gh-9263).
