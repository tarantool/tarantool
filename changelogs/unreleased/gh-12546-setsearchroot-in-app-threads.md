## bugfix/core

* The root directory from which Lua dependencies are loaded was made global:
  setting it with `package.setsearchroot()` in any thread now affects all
  threads (gh-12546).
