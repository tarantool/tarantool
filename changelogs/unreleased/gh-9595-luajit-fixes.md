## bugfix/luajit

Backported patches from the vanilla LuaJIT trunk (gh-9595). The following
issues were fixed as part of this activity:

* Fixed stack-buffer-overflow for `string.format()` with `%g` modifier and
  length modifier.
