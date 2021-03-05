## feature/core

*  Previously lua on_shutdown triggers were started sequentially, now
   each of triggers starts in a separate fiber. Tarantool waits for 3.0
   seconds to their completion by default. User has the option to change
   this value using new implemented box.ctl.set_on_shutdown_timeout function.
   If timeout has expired, tarantool immediately stops, without waiting for
   other triggers completion.
