## feature/debugger

* Added the support for breakpoints to the builtin console debugger
  `luadebug.lua`.

* Made is easier to debug files with the same name (such as `init.lua`)
  by handling partial path lookup in breakpoints:

    ```
    break B/init.lua:10
    break A/init.lua:20
    break ./main.lua:30
    break ../a/b/c/leaf.lua:40
    ```
