## bugfix/sql

* Now function `quote()` will return an argument in case the argument is
  `DOUBLE`. The same for all other numeric types. For types other than numeric,
  `STRING` will be returned (gh-6239).

