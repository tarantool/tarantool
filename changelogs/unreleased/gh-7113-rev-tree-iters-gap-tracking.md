## bugfix/core

* Fixed reversed iterators gap tracking: instead of tracking gaps for
  successors of keys, gaps for tuples shifted by one to the left of
  the successor were tracked (gh-7113).
