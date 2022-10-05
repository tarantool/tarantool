## bugfix/luajit

Backported patches from vanilla LuaJIT trunk (gh-7230). In the scope of this
activity, the following issues have been resolved:

* Fix overflow check in `unpack()` optimized by a compiler.
* Fix recording of `tonumber()` with cdata argument for failed conversions
  (gh-7655).
* Fix concatenation operation on cdata. It always raises an error now.
