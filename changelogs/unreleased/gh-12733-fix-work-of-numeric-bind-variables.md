## bugfix/sql

* Fixed matching of SQL bind variables to values: `$N` now always takes
  the N-th value, `:name`, `@name`, and `#name` take the element with that
  name, and `?` takes the value after the one taken by the previous variable.
  `$N` can no longer be bound by a `{['$N'] = value}` element (gh-12733).
