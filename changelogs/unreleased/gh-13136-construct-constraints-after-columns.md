## bugfix/sql

* Now a table constraint will find the columns it uses, even if those
  columns are defined after the constraint (gh-13136).
