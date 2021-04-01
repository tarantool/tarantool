## bugfix/core

* Fix crash in case of reloading a compiled module when the
  new module lacks some of functions which were present in the
  former code. In turn this event triggers a fallback procedure
  where we restore old functions but instead of restoring each
  function we process a sole entry only leading to the crash
  later when these restored functions are called (gh-5968).
