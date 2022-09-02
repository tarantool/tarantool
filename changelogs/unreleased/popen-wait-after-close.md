## bugfix/lua/popen

* Defined the behavior of in-progress `<popen handle>:wait()` when
  `<popen handle>:close()` is called in another fiber: now it returns an error
  instead of accessing a freed memory and, likely, hanging forever (gh-7653).
