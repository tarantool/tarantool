## feature/log

* Warning and error logs written to a terminal on stderr are now colored.
  Set the `NO_COLOR` environment variable to any non-empty value to disable colors.
  Colors are also disabled when `TERM=dumb`. JSON logs remain uncolored (gh-10250).
