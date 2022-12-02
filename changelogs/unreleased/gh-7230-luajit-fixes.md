## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following issues have been resolved:

* Fix handling of errors during trace snapshot restore.
* Fix overflow check in `unpack()` optimized by a compiler.
* Fix recording of `tonumber()` with cdata argument for failed conversions
  (gh-7655).
* Fix concatenation operation on cdata. It always raises an error now.
* Fix `io.close()` for already closed standard output.
* Fix trace execution and stitching inside vmevent handler (gh-6782).
* Fixed `emit_loadi()` on x86/x64 emitting xor between condition check
  and jump instructions.
* Fix stack top for error message when raising the OOM error (gh-3840).
* Fix assembling of type-check-only variant of `IR_SLOAD`.

## feature/luajit
Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following features is completed:

* `assert()` now accepts any type of error object (from Lua 5.3).
