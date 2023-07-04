## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8516). The following issues
were fixed as part of this activity:

* Fixed `IR_LREF` assembling for the GC64 mode on x86_64.
* Fixed canonicalization of +-0.0 keys for `IR_NEWREF`.
* Fixed result truncation for `bit.rol` on x86_64 platforms.
* Fixed saved bytecode encapsulated in ELF objects.
* Fixed `lua_yield()` invocation inside C hooks.
* Fixed memory chunk allocation beyond the memory limit.
* Fixed TNEW load forwarding with instable types.
* Fixed use-def analysis for `BC_VARG`, `BC_FUNCV`.
