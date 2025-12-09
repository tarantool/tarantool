## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-11691). The following
issues were fixed as part of this activity:

* Fixed incorrect emitting for `IR_TBAR` on aarch64.
* Fixed stack overflow handling for the trace exit.
* Fixed dangling `CType` references.
* Fixed closing VM state after early OOM.
* Fixed emitting for `IR_MUL` on x86/x64.
* Fixed incorrect `stp`/`ldp` instructions fusion on aarch64.
* Fixed SCEV entry invalidation when returning to a lower frame.
* Fixed macOS 15 / Clang 16 build.
* Fixed emitting for `IR_HREFK` on aarch64.
