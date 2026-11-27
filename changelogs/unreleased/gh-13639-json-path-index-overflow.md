## bugfix/core

* Fixed a signed integer overflow while parsing a numeric JSON path index.
  Indices outside the supported `int` range are now rejected instead of
  silently wrapping around (gh-13639).
