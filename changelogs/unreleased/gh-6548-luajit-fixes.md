## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-6548). In the scope of this
activity, the following issues have been resolved:

* Fixed emitting for fuse load of constant in GC64 mode (gh-4095, gh-4199, gh-4614).
* Now initialization of zero-filled struct is compiled (gh-4630, gh-5885).
* Actually implemented `maxirconst` option for tuning JIT compiler.
* Fixed JIT stack of Lua slots overflow during recording for metamethod calls.
* Fixed bytecode dump unpatching for JLOOP in up-recursion compiled functions.
* Fixed FOLD rule for strength reduction of widening in cdata indexing.
* Fixed `string.char()` recording without arguments.
* Fixed `print()` behaviour with the reloaded default metatable for numbers.
* `tonumber("-0")` now saves the sign of number for conversion.
* `tonumber()` now give predictable results for negative non-base-10 numbers.
* Fixed write barrier for `debug.setupvalue()` and `lua_setupvalue()`.
* `jit.p` now flushes and closes output file after run, not at program exit.
* Fixed `jit.p` profiler interaction with GC finalizers.
