## feature/luajit

* Now memory profiler dumps symbol table for C functions. As a result memory
  profiler parser can enrich its symbol table with C symbols (gh-5813).
  Furthermore, now memprof dumps special events for symbol table when it
  encounters a new C symbol, that hasn't been dumped yet.
