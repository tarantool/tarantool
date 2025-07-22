## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-11278). The following
issues were fixed as part of this activity:

* Fixed JIT slot overflow during recording of trace with up-recursion.
* Fixed stack overflow handling.
* Fixed potential file descriptor leaks in `loadfile()`.
* Fixed error generation in `loadfile()`.
* Fixed incorrect snapshot restore due to stack overflow.
* Fixed assembling of IR SLOAD for the aarch64 architecture.
* Fixed assembling of IR HREFK for the aarch64 architecture.
* Fixed incorrect `stp`/`ldp` instructions fusion on aarch64.
