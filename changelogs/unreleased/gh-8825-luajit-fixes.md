## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-8825). The following issues
were fixed as part of this activity:

* Fixed `BC_UCLO` insertion for returns.
* Fixed recording of `BC_VARG` with unused vararg values.
* Initialization instructions on trace are now emitted only for the first
  member of a union.
* Fixed load forwarding optimization applied after table rehashing.
