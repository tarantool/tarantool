## bugfix/lua/fio

* The default permission mode for `fio.open()` was changed for newly
  created files to 0666 (before umask) (gh-7981).
