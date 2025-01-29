## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-10709). The following
issues were fixed as part of this activity:

* Fixed compilation of `getmetatable()` for `io` objects.
* Fixed dirty reads from recorded `IR_NOP`.
* Fixed fusing optimization across `table.clear()` or insertion of a new key.
* Disabled FMA optimization on aarch64 to avoid incorrect results in floating
  point arithmetics. Optimization may be enabled for the JIT engine via the
  command `jit.opt.start("+fma")`.
* Fixed machine code zone overflow for trace recording on x86/x64.
* Fixed possible infinite loop during recording a chunk that uses upvalues.
* Fixed recording of `bit.bor()`/`bit.bxor()`/`bit.band()` with string
  arguments.
* Fixed parsing of `for _ in` loop.
