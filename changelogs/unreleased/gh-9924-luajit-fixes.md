## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9924). The following issues
were fixed as part of this activity:

* Fixed `BC_VARG` recording.
* Fixed `ffi.alignof()` for reference types.
* Fixed `sizeof()` expression in C parser for reference types.
* Fixed `ffi.metatype()` for typedefs with attributes.
* Fixed `ffi.metatype()` for non-raw types.
* Fixed IR chain invariant in DCE.
* Fixed OOM errors handling during trace stitching.
* Fixed `IR_HREF` vs. `IR_HREFK` aliasing in non-`nil` store check.
* Fixed generation of Mach-O object files.
* Fixed undefined behavior when negating `INT_MIN` integers.
* Replaced the numeric values of NYI bytecodes that can't be compiled, with
  their names in the `jit.dump()`.
