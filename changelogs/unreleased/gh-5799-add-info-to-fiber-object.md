## feature/lua/fiber

* Introduce `fiber_object:info()` to get `info` from fiber. Works as `require(fiber).info()` but only for one fiber.
* Introduce `fiber_object:csw()` to get `csw` from fiber (gh-5799).
