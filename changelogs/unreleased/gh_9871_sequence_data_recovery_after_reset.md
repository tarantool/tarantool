## bugfix/core

* Fixed a bug that resulted in `sequence:reset()` call result not saved
  in WAL and snapshot and thus not recovered after the server restart
  (gh-9871).
