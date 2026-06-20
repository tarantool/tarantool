## bugfix/box

* Fixed an issue where zero values were incorrectly rejected by fixed-point
  decimal fields when their scale was greater than or equal to their precision
  (gh-12808).
