## bugfix/sql

* A named bind variable must now always begin with `#`, `:`, or `@` followed by
  a letter. A numeric bind argument must begin with `$` followed by a number.
  An anonymous bind variable is denoted `?` followed by a non-numeric,
  non-alphabetic character (gh-13012).
