## bugfix/sql

* A named bind variable must now always begin with `#`, `:`, or `@` followed by
  a letter or a non-digit identifier character (variants such as `@number` are
  no longer supported). A numeric bind variable must begin with `$` followed by
  a number (variants such as `$name` are no longer supported). An anonymous
  bind variable is denoted by `?` followed by a non-numeric, non-alphabetic
  character (gh-13012).
