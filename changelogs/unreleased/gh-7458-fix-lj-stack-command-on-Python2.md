## bugfix/luajit

* Fixed the Lua stack dump command (`lj-stack`) not working on Python 2.
  Previously, it used arguments unpacking within the list initialization, which
  is not supported in Python 2 (gh-7458).
