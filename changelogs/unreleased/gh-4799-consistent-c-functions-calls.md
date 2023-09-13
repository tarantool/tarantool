## bugfix/box

* **[Breaking change]** Added a `c_func_iproto_multireturn` option to the
  `compat` module. The new behavior drops an additional array that wraps
  multiple results returned via iproto (gh-4799).
