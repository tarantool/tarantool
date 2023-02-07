## bugfix/core

* Loosen requirements for tuple which is used as position - passed to
  `index:tuple_pos()` or to option `after` of `index:select`. Now, only
  key parts of current and primary indexes are validated (gh-8511).
