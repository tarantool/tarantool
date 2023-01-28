## feature/debugger

* Added breakpoints support to the builtin console debugger `luadebug.lua`;
* To support easier debugging of files with the same name (i.e. `init.lua`)
  breakpoints handle partial path lookup in a form:

    break B/init.lua:10
    break A/init.lua:20
