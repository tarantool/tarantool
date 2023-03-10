## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-8069). In the scope of this
activity, the following issues have been resolved:

* Fixed successful `math.min/math.max` call with no args (gh-6163).
* Fixed inconsistencies in `math.min/math.max` calls with NaN arg (gh-6163).
* Fixed `pcall()` call without arguments on arm64.
* Fixed assembling of IR_{AHUV}LOAD specialized to boolean for aarch64.
* Fixed constant rematerialization on arm64.
