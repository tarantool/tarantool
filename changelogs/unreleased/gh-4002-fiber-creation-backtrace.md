## feature/core

 * Added new entries into subtable `backtrace` to the `fiber.info()`
   corresponding to the C and Lua backtraces of fiber creation and
   `fiber.parent_bt_enable()`/`fiber.parent_bt_disable()` options in order to
   switch on/off the ability to collect parent backtraces for newly created
   fibers and to show/hide those entries in the `fiber.info()`.
