## feature/lua

* Introduced the Tarantool compatibility module `tarantool.compat`.
  It is used for transparent management of behavior changes.
  `tarantool.compat` stores options that reflect behavior changes. Possible
  option values are `old`, `new`, and `default`. By default, `tarantool.compat`
  contains options for certain Tarantool changes that may break compatibility
  with previous versions. Users can also add their own compatibility options in
  runtime (gh-7000).
