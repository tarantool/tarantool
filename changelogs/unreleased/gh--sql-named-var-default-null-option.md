## bugfix/sql

* Added an error that is thrown when a value cannot be found
  for a named variable in a query using bound variables.
  The error message: "No value for parameter '%s'" (gh-).

* Added a new option, `sql_named_var_default_null`, allows this error
  to be disabled. When disabled, any named variable for which no value is found
  will default to `NULL`. By default, this option is set to `false`,
  meaning the error will be issued (gh-).
