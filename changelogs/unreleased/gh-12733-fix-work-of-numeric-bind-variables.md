## bugfix/sql

* Now the number of a numeric variable always corresponds to its position
  in the array of bind variables.
  The anonymous variable always corresponds to the next variable.
  The named bind variables always corresponds to a variable with the same name
  in the array of bind variables (gh-12733).
