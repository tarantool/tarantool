## feature/core

* Introduced the new read view method `:with()`, which calls a function
  with a guarantee that the read view object will not be closed until
  the function returns (gh-13037).
