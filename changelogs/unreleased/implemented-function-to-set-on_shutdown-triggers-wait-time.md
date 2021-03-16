## feature/core

*  Implemented function to set on_shutdown triggers wait time.
   Currently tarantool waits for on_shutdown triggers completion
   for 3.0 seconds, this patch implemented feature that allows user
   to set this time by itself.
