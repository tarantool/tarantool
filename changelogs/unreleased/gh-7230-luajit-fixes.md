## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following issues have been resolved:

* Fix `io.close()` for already closed standard output.
* Fix trace execution and stitching inside vmevent handler (gh-6782).
* Fixed `emit_loadi()` on x86/x64 emitting xor between condition check
  and jump instructions.
* Fix stack top for error message when raising the OOM error (gh-3840).
* Enabled external unwinding on several LuaJIT platforms. Now it is possible to
  handle ABI exceptions from Lua code (gh-6096).
* Disabled math.modf compilation due to its rare usage and difficulties with
  proper implementation of the corresponding JIT machinery.
* Fixed inconsistent behaviour on signed zeros for JIT-compiled unary minus
  (gh-6976).
* Fixed `IR_HREF` hash calculations for non-string GC objects for GC64.
* Fixed assembling of type-check-only variant of `IR_SLOAD`.

## feature/luajit
Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following features is completed:

* `assert()` now accepts any type of error object (from Lua 5.3).
