## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-12880). The following
issues were fixed as part of this activity:

* Fixed stack overflow relimit handling.
* Fixed container of packed bitfield.
* Fixed `fp:seek()` with the string argument.
* Fixed tobit conversions for aarch64 in the VM.
* Fixed pointer difference operation on 64-bit platforms.
