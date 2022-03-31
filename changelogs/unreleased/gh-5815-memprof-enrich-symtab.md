## feature/luajit

* Now the memory profiler emits events of the new type when a function or
  a trace is created. As a result the memory profiler parser can enrich its
  symbol table with the new functions and traces (gh-5815).

  Furthermore, there are symbol generations introduced within the internal
  parser structure to handle possible collisions of function addresses and
  trace numbers.
