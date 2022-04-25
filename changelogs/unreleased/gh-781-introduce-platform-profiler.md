## feature/luajit
* Introduced the LuaJIT platform profiler (gh-781) and the profile parser.
This profiler is able to capture both host and VM stacks, so it can show the
whole picture. Both C and Lua API's are available for the profiler.<br/>
Profiler comes with the default parser, which produces output in a
flamegraph.pl-suitable format.<br/>
The following profiling modes are available:
  - Default: only virtual machine state counters
  - Leaf: shows the last frame on the stack
  - Callchain: performs a complete stack dump
