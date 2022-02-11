## bugfix/luajit

* Fixed top part of Lua stack (red zone, free slots, top slot) unwinding in
  `lj-stack` command.
* Added the value of `g->gc.mmudata` field to `lj-gc` output.
