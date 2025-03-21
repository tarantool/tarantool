## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-11055). The following
issues were fixed as part of this activity:

* Fixed closing the report file without samples for `jit.p`.
* Fixed the OOM error handling during recording of the `__concat` metamethod.
* Fixed the second `trace.flush()` call for the already flushed trace.
* Fixed bit op coercion for shifts in DUALNUM builds.
* Fixed `IR_ABC` hoisting.
* Returned the rehashing of the cdata finalizer table at the end of the GC
  cycle to avoid memory overgrowing for cdata-intensive workloads.
