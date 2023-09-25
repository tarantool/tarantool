## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8825). The following issues
were fixed as part of this activity:

* Fixed the panic routine when `mprotect` fails to change flags for mcode area.
* Fixed handling of instable types in TNEW/TDUP load forwarding.
* Handled table unsinking in the presence of `IRFL_TAB_NOMM`.
