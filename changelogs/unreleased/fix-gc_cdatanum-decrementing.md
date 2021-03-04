## bugfix/LuaJIT

* Fixed double `gc_cdatanum` decrementing in LuaJIT platform metrics when a
  finalizer is set for GCcdata object (gh-5820).
