## bugfix/lua/merger

* Fixed use-after-free during iteration over `merge_source:pairs()` or
  `merger:pairs()` (gh-7657).
