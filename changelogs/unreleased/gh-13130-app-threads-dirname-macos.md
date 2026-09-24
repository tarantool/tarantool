## bugfix/core

* Fixed application thread startup failures caused by concurrent module path
  lookup on macOS, and a crash in `fio.dirname()` when the directory part of a
  path is at least 1024 bytes long (gh-13130).
