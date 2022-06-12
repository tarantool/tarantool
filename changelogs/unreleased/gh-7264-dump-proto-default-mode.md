## bugfix/luajit

* Disabled proto and trace information dumpers in sysprof's
default mode. Attempts to use them lead to segmentation fault
due to uninitialized buffer.
