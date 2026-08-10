## feature/sql

* When the option is `old`, the search id first looks for an exact match;
  if that fails, it searches using the id converted to uppercase.
  When the option is `new`, the search looks only for an exact name match.
  The default setting is `old`. This can be toggled using the command
  `compat.sql_uppercase_id = option` (gh-12732).
