## feature/luajit

* Make LuaJIT memory profiler parser output more user-friendly (gh-5811). Now
  the source line definition where the event occurs is much clearer: only
  source file name and allocation-related line are presented, the function
  definition line number is omitted. Moreover, event-related statistics are
  indicated with units.
  **Breaking change**: Line info of the line function definition is saved in
  symbol info table by field `linedefined` now and field `name` is renamed to
  `source` with the respect to the Lua Debug API.
