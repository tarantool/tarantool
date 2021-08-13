## bugfix/sql

* Now function quote() will return the argument in case the argument is DOUBLE.
  The same for all other numeric types. For types other than numeric, STRING
  will be returned (gh-6239).

