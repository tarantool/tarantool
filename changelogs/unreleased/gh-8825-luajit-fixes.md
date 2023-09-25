## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8825). The following issues
were fixed as part of this activity:

* Prevent integer overflow while parsing long strings.
* Fixed various `^` operator and `math.pow()` function inconsistencies.
* Fixed parsing with predicting `next()` and `pairs()`.
* Fixed binary number literal parsing. Parsing of binary number with a zero
  fractional part raises error too now.
* Fixed load forwarding optimization applied after table rehashing.
* Fixed recording of the `BC_TSETM`.
* Fixed the panic routine when `mprotect` fails to change flags for mcode area.
* Fixed handling of instable types in TNEW/TDUP load forwarding.
* Handled table unsinking in the presence of `IRFL_TAB_NOMM`.
