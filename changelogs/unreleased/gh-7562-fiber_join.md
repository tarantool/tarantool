## bugfix/core

* Fixed a possible inconsistent state entering if fibers are joined incorrectly.
  Now the `fiber_set_joinable` function panics if the fiber is dead or joined
  already. The `fiber_join` and `fiber_join_timeout` functions now panic on a
  double join if it is possible to detect it (gh-7562).
