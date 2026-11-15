## bugfix/lua

* Fixed rocks lookup searching the wrong directory or skipping parent
  directories when the module search root contains a symlink (gh-13086).
* Fixed direct Lua and native module lookup failing for search roots such
  as `missing/../wd` when `missing` does not exist (gh-13086).
