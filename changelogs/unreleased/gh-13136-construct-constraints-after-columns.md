## bugfix/sql

* Now a table constraint will find columns it uses, even if those columns
  are defined after the constraint (gh-13136).
