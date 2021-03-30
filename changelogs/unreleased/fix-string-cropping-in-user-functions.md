## bugfix/sql

* The string received by the user-defined C or Lua function could be different
  from the string passed to the function. This could happen if the string passed
  from SQL contains '\0' (gh-5938).
