## feature/box/lua

* Critical log entry, containing the current stack traceback, upon empty or nil 
  :select and :pairs calls on user spaces (gh-6539).

## feature/box/lua/log
* log.crit() writes a user-generated message to the corresponding log
  file with CRITICAL log level.
