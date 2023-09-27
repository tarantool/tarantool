## bugfix/box

* Fixed crashes if `box.info.memory()`, `box.info.gc()`, `box.info.vinyl()`,
  and `box.info.sql()` are called before `box.cfg{}` (gh-9173).
