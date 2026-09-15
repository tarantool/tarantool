## bugfix/sql

* Now the number of a numeric bind variable always corresponds to its position
  in the provided array of bind variables; the anonymous bind variable always
  corresponds to the next variable to be processed in the provided array
  of bind variables. Due to the query planner, the execution order of variables
  may differ from their order of appearance in the query;
  therefore, in some queries, they should be replaced with numbered or named
  variables; the named bind variables always corresponds to a variable
  with the same name in the provided array of bind variables (gh-12733).
