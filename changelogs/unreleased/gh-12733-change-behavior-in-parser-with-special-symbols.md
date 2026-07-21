## bugfix/sql

* During parsing bind variable we expect that
  #, :, @ are used for designate named variables,
  $ is used for designate positional variables
  (after this can follow onle numbers),
  ? for anonymous variables.
