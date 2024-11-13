## bugfix/vinyl

* Fixed a bug when an attempt to alter the primary index of an empty space
  triggered a crash if executed concurrently with a DML request (gh-10603).
