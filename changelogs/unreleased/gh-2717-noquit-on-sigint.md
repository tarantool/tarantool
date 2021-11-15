## bugfix/lua

* Fixed the behavior of tarantool console on SIGINT. Now Ctrl+C discards
  the current input and prints the new prompt (gh-2717).