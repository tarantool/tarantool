## bugfix/replication

* Fixed a possible split-brain when old synchro queue owner might finalize the
  transactions in presence of a new synchro queue owner (gh-5295).
