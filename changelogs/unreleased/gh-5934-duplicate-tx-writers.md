# bugfix/vinyl

* Fix crash which may occur while switching read_only mode due to duplicating
  transaction in tx writer list (gh-5934).
