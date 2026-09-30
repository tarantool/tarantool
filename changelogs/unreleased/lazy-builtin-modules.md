## feature/lua

* Built-in Lua modules are now loaded at their first `require()` rather than
  at startup, which cuts the startup time of a script by more than half.
  An override of a built-in module that another built-in module used to
  require at startup (for example, `config`) is now looked up at the first
  `require()` of the overridden module as well.
  C declarations that a built-in module makes with `ffi.cdef()` appear when
  the module is loaded, so code that uses them without declaring them has to
  declare them or to require the module first.
