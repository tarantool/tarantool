## feature/luajit

* Introduced preliminary support of GNU/Linux ARM64 and MacOS M1. In scope of
  this activity the following issues have been resolved:
  - Introduced support for full 64-bit range of lightuserdata values (gh-2712)
  - Fixed memory remapping issue when the page leaves 47-bit segments
  - Fixed M1 architecture detection (gh-6065)
  - Fixed variadic arguments handling in FFI on M1 (gh-6066)
  - Fixed `table.move` misbehaviour when table reallocation occurs (gh-6084)
  - Fixed Lua stack inconsistency when xpcall is called with invalid second
    argument on ARM64 (gh-6093)
  - Fixed `BC_USETS` bytecode semantics for closed upvalues and gray strings
  - Fixed side exit jump target patching considering the range values of the
    particular instruction (gh-6098)
  - Fixed current Lua coroutine restoring on exceptional path on ARM64 (gh-6189)
