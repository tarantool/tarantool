## bugfix/lua

* Fixed loading of Lua modules after the current working directory of an
  instance is removed: `require()` failed with `fio.pathjoin(): undefined path
  part 1` (gh-13488).
