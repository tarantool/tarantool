## bugfix/memtx

* Fixed a bug when altering a multikey index and creating a new one over
  the same field could lead to a crash or undefined behavior (gh-11291).
