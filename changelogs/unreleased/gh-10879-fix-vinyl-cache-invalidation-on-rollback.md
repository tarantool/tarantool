## bugfix/vinyl

* Fixed a bug when the tuple cache was not properly invalidated in case
  a WAL write error occurred while committing a `space.delete()` operation.
  The bug could lead to a crash or an invalid read query result (gh-10879).
