## bugfix/lua

* Fixed -e option, when tarantool always entered interactive mode
  when stdin is a tty. Now, `tarantool -e 'print"Hello"'` doesn't
  enter interactive mode as it was before, just prints 'Hello' and
  exits (gh-5040).
