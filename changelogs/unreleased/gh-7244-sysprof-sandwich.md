## bugfix/luajit

* Added `/proc/self/exe` symlink resolution to the
symtab module, to obtain the .symtab section for
tarantool executable. Added stack sandwich support
to sysprof's parser.
