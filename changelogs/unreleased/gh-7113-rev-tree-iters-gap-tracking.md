## bugfix/core

* Fixed reversed iterators gap tracking. Instead of tracking gaps for
  the successors of keys, gaps for tuples shifted by one to the left of
  the successor were tracked (gh-7113).
