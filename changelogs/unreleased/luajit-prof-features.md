## feature/tools

Made LuaJIT profilers more user-friendly:
* `misc.memprof.start()` without arguments writes the dump into the default file
  named `memprof.bin` instead of raising an error.
* `misc.sysprof.start()` provides more verbose errors in case of profiler
  misuse.
* If the profiler is disabled for the target platform, it is now mentioned in
  the error message explicitly.
* `misc.sysprof.start()` without arguments starts the profiler in the default
  mode `"D"`.
