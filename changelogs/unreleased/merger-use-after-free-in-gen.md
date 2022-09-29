## bugfix/lua/merger

* Fixed `use after free` that could occur during iteration over `merge_source:pairs()` or
  `merger:pairs()` (gh-7657).
