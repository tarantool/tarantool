## bugfix/misc

* Fixed the Docker image initializing a database when running Lua scripts or
  utilities. Automatic server settings now apply only to server invocations;
  explicitly supplied settings remain honored (gh-9993).
