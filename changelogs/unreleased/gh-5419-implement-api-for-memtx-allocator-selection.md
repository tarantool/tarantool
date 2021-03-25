## feature/core

* Implement api for memtx allocator selection. Add new 'memtx_allocator' option
  to box.cfg{} which allows to select the appropriate allocator for memtx tuples
  if necessary. Possible values are "system" for malloc allocator and "small" for
  default small allocator (gh-5419).
