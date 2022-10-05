## bugfix/luajit

Fix Lua stack dump command (`lj-stack`), since unpacking arguments within the
list initialization is not supported in Python 2 (gh-7458).
