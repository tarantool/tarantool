## bugfix/core

* If an error occurs during a snapshot recovery, the row that caused it is
  now written in the log (gh-7917).
