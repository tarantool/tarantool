## bugfix/luajit

* Fixed slots alignment in `lj-stack` command output when `LJ_GC64` is enabled
  (gh-5876).

* Fixed dummy frame unwinding in `lj-stack` command.

* Fixed top part of Lua stack (red zone, free slots, top slot) unwinding in
  `lj-stack` command.

* Added the value of `g->gc.mmudata` field to `lj-gc` output.
