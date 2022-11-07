## feature/box

* **[Breaking change]** Now the log message contains the name of a module from
  which the logging function was called (gh-3211).

* Supported per-module log level setting via `log.cfg{modules = {...}}` or via
  `box.cfg{log_modules = {...}}` (gh-3211).
