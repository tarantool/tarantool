## feature/box

* Added the `yield_period` option to `index:pairs()` and `space:pairs()`. When
  set to a positive integer `N`, the iterator reschedules the current fiber once
  every `N` tuples it returns, so that a long scan over a space cooperates with
  the event loop instead of hogging the cord. The default (`0`/`nil`) keeps the
  previous behavior of never yielding on its own.
