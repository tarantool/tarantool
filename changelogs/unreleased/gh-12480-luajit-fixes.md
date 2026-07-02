## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-12480). The following
issues were fixed as part of this activity:

* Fixed incorrect JIT behavior for vararg FFI functions on the macOS AArch64
  platform (gh-6097).
* Fixed various FFI ABI and calling convention issues for x64/AArch64
  architectures.
* Fixed `ipairs_aux()` to match JIT backend behavior on x86/x64.
* Fixed `os.time()` returning `-1`.
* Fixed UBSan warnings for `table.new()`.
