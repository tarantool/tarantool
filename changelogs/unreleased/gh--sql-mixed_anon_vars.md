## bugfix/sql

* Added an error that is thrown when anonymous bound variables are mixed
  with named or numbered variables. The error message:
  "Mixing anonymous variables with other types of variables is not permitted"
  (gh-).

* Added a new option, `sql_mixed_anon_vars`, allows this error
  to be disabled. When option is `true`, no error message is displayed,
  and the request completes successfully. By default, this option is set
  to `false`, meaning the error will be issued (gh-).
