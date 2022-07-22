## bugfix/luajit

* Fixed the top part of Lua stack (red zone, free slots, top slot) unwinding in
  the `lj-stack` command.
* Added the value of `g->gc.mmudata` field to `lj-gc` output.
