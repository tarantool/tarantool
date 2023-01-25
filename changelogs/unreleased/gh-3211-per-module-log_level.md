## feature/core

* Now the log message contains the name of a Lua module from which the logging
  function was called (gh-3211).

* Now the log level can be set for specific modules using `log.cfg{modules = {...}}`
  or `box.cfg{log_modules = {...}}` (gh-3211).
