## bugfix/lua

* Rocks lookup now respects the filesystem target of a module search root
  containing a symlink followed by a parent component. This also preserves
  initial module-dependent configuration defaults for such instance workdirs.
  Lookup through missing directories canceled by parent components remains
  supported, including before the instance workdir is created (gh-13086).
