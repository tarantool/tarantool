## bugfix/build

* Bump debian packages tarantool-common dependency
  to use luarocks 3 (gh-5429).

  Fixes an error when it was possible to have new tarantool
  package (version >= 2.2.1) installed with pre-luarocks 3
  tarantool-common package (version << 2.2.1), which caused
  rocks install to fail.
