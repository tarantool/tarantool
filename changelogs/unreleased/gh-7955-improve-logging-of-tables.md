## bugfix/core

* Fixed modification of a variable with a table passed to the logging function.
  Fixed the drop of fields with reserved internal names (plain log format).
  Added 'message' field to tables without that field in JSON log format
  (gh-3853).

* Fixed an assertion on malformed JSON message written to the log (gh-7955).
