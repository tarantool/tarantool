## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8069).
The following issues were fixed as part of this activity:

* Fixed successful `math.min/math.max` call with no args (gh-6163).
* Fixed inconsistencies in `math.min/math.max` calls with a NaN arg (gh-6163).
* Fixed `pcall()` call without arguments on arm64.
* Fixed assembling of ``IR_{AHUV}LOAD`` specialized to boolean for aarch64.
* Fixed constant rematerialization on arm64.
* Fixed `emit_rma()` for the x64/GC64 mode for non-`mov` instructions.
* Limited Lua C library path with the default `PATH_MAX` value of 4096 bytes.
