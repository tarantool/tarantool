## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following issues have been resolved:

* Fix `io.close()` for already closed standard output.
* Fix trace execution and stitching inside vmevent handler (gh-6782).
* Fixed `emit_loadi()` on x86/x64 emitting xor between condition check
  and jump instructions.
* Fix stack top for error message when raising the OOM error (gh-3840).
