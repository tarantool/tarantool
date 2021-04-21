# bugfix/core

* Now tarantool yields when scanning `.xlog` files for the latest applied vclock
  and when finding the right place in `.xlog`s to start recovering. This means
  that the instance is responsive right after `box.cfg` call even when an empty
  `.xlog` was not created on previous exit.
  Also this prevents relay from timing out when a freshly subscribed replica
  needs rows from the end of a relatively long (hundreds of MBs) `.xlog`
  (gh-5979).

* The counter in `x.yM rows processed` log messages does not reset on each new
  recovered `xlog` anymore.
