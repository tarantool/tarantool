## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-10199). The following
issues were fixed as part of this activity:

* Now 64-bit non-FAT Mach-O object files are generated via `-b -o osx`.
* Fixed `string.format()` compilation with many elements.
* Fixed `dlerror()` in FFI call returning `NULL`.
* Fixed `__tostring` metamethod access to enum cdata value.
* Fixed limit check in narrowing optimization.
* Dropped finalizer table rehashing after GC cycle (gh-10290).
* Fixed recording of `select(string, ...)`.
* Fixed stack allocation after on-trace stack check.
* Fixed recording of `__concat` metamethod that throws an error.
* Fixed bit op coercion in `DUALNUM` builds.
* Fixed 64-bit shift fold rules.
* Fixed loop optimizations for cdata arguments of vararg FFI functions.
