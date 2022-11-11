## bugfix/luajit

* Fixed the Lua stack dump command (`lj-stack`) to support Python 2: unpacking
  arguments within the list initialization is not supported in it (gh-7458).
