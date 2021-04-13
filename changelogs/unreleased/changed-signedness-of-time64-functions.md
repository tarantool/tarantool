## bugfix/core

* **[Breaking change]** Return value signedness of 64-bit time functions in
  `clock` and `fiber` was changed from `uint64_t` to `int64_t` both in Lua
  and C (gh-5989).

----

Breaking change: lua: return value type for all time64 functions changed from
`uint64_t` to `int64_t`.
