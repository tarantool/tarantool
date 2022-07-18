## bugfix/luajit

* Fixed the case for partial recording of vararg function body with the fixed
  number of result values in with `LJ_GC64` (i.e. `LJ_FR2` enabled) (gh-7172).
