## feature/core

* The box error C API (`box_error_set()`, `box_error_last()`, etc) can now be
  used in threads started by user modules with the pthread library (gh-7814).
