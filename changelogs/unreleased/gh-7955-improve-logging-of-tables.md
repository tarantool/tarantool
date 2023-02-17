## bugfix/core

* Fixed a bug when fields could be removed from a table stored in a variable
  when a logging function was called on this variable (for example,
  `log.info(a)`) (gh-3853).

* Fixed a logging bug: when logging tables with fields that have reserved
  internal names (such as `pid`) in the plain log format, such fields weren't
  logged (gh-3853).

* Added the `message` field when logging tables without such field in the JSON
  log format (gh-3853).

* Fixed an assertion on malformed JSON message written to the log (gh-7955).
