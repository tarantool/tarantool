## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9595). The following issues
were fixed as part of this activity:

* No side traces are recorded now after disabling the JIT via `jit.off()`.
* Fixed handling of instable boolean types in TDUP load forwarding.
* Fixed a crash during the restoration of the sunk `TNEW` with a huge array
  part.
* Fixed stack-buffer-overflow for `string.format()` with `%g` modifier and
  length modifier.
* Fixed recording of `setmetatable()` with `nil` as the second argument.
* Fixed recording of `select()` in case with negative first argument.
* Fixed use-def analysis for child upvalues.
* Added the `cc` file type for saving bytecode.
* Fixed C file generation in `jit.bcsave`.
* Fixed trace error handling during trace stitching.
* Fixed recording of the `__concat` metamethod for vararg or protected frames.
* Fixed recording of a side trace returning to a lower frame with a maximum
  possible frame size.
