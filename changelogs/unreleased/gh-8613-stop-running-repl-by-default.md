## feature/cli

* **[Behavior change]** Disabled starting the Lua REPL by default when running
  Tarantool. Now, Tarantool yields the message that shows the command usage.
  To run the Lua REPL, just set the `-i` flag. To pass a Lua script contents via
  `stdin`, use dash (`-`) as the script name. For more information see a help
  message by running `tarantool -h` (gh-8613).
