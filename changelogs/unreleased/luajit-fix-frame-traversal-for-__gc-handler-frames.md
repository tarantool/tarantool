## bugfix/luajit

* Fixed inconsistency while searching for an error function when unwinding
  a C-protected frame to handle a runtime error (e.g. an error in __gc handler).
