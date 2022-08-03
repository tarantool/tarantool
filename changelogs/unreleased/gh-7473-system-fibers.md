## feature/core

* Some of the internal fibers (e.g. connection's worker fiber, vinyl fibers
  and others) cannot be cancelled from the Lua public API anymore (gh-7473).
