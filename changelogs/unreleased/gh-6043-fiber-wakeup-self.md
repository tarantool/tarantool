## bugfix/core

* **[Breaking change]** `fiber.wakeup()` in Lua and `fiber_wakeup()` in C became
  NOP on the currently running fiber. Previously they allowed to "ignore" the
  next yield or sleep leading to unexpected spurious wakeups. Could lead to a
  crash (in debug build) or undefined behaviour (in release build) if called
  right before `fiber.create()` in Lua or `fiber_start()` in C (gh-6043).

  There was a single usecase for that - reschedule in the same event loop
  iteration which is not the same as `fiber.sleep(0)` in Lua and
  `fiber_sleep(0)` in C. Could be done in C like that:
  ```C
  fiber_wakeup(fiber_self());
  fiber_yield();
  ```
  and in Lua like that:
  ```Lua
  fiber.self():wakeup()
  fiber.yield()
  ```
  Now to get the same effect in C use `fiber_reschedule()`. In Lua it is now
  simply impossible to reschedule the current fiber in the same event loop
  iteration directly. But still can reschedule self through a second fiber like
  this (**never use it, please**):
  ```Lua
  local self = fiber.self()
  fiber.new(function() self:wakeup() end)
  fiber.sleep(0)
  ```

----
Breaking change: `fiber.wakeup()` in Lua and `fiber_wakeup()` in C became NOP on
the currently running fiber.
