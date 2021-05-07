## feature/lua/log

 * Implemented support of symbolic log levels representation
   in `log` module (gh-5882). Now it is possible to specify
   levels the same way as in `box.cfg{}` call. For example
   instead of
   ``` Lua
   require('log').cfg{level = 6}
   ```
   One can use
   ``` Lua
   require('log').cfg{level = 'verbose'}
   ```
