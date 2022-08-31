## feature/core

* Certain internal fibers, such as the connection's worker fiber, vinyl fibers,
  and some other fibers, cannot be cancelled from the Lua public API anymore (gh-7473).
