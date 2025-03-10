## bugfix/tools

Fixed a bunch of bugs in LuaJIT profilers:
* `misc.sysprof.stop()` returns a correct error message if the profiler is not
  running.
* `misc.sysprof.start()` now raises an error if an argument has an incorrect
  type.
