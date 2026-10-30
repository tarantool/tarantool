## bugfix/lua

* Rocks lookup now respects the filesystem target of a module search root
  containing a symlink followed by a parent component. Lookup through missing
  directories canceled by parent components remains supported (gh-13086).
