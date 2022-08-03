## bugfix/luajit

* Disabled proto and trace information dumpers in sysprof's default mode.
  Attempts to use them lead to a segmentation fault due to an uninitialized buffer
  (gh-7264).
