## feature/sql

* Added implementation of threaded interpretation in SQL VM,
  introduced `option ENABLE_VDBE_GOTO_DISPATCH` in the root
  CMakeLists.txt to use threaded interpretation instead of
  switch-case method (gh-4212).
