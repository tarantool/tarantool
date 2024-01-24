## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9595). The following
issues were fixed as part of this activity:

* Fixed a crash during the restoration of the sunk `TNEW` with a huge array
  part.
