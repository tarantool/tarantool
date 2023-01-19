## feature/lua

* It is now possible to run scripts or load modules before the main script by
  specifying them in the `TT_PRELOAD` environment variable. For example:

  ```shell
  $ TT_PRELOAD=/path/to/foo.lua tarantool main.lua
  ```

  (gh-7714).
