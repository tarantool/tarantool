## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following issues have been resolved:

* Fix overflow check in `unpack()` optimized by a compiler.
* Fix recording of `tonumber()` with cdata argument for failed conversions
  (gh-7655).
* Fix concatenation operation on cdata. It always raises an error now.
* Fix `io.close()` for already closed standard output.
* Fix trace execution and stitching inside vmevent handler (gh-6782).
* Fixed `emit_loadi()` on x86/x64 emitting xor between condition check
  and jump instructions.
* Fix stack top for error message when raising the OOM error (gh-3840).
* Disabled math.modf compilation due to its rare usage and difficulties with
  proper implementation of the corresponding JIT machinery.
* Fixed inconsistent behaviour on signed zeros for JIT-compiled unary minus
  (gh-6976).
