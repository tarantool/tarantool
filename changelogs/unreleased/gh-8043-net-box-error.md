## bugfix/core

* Fixed a crash in `net.box` that happened if the error message raised by
  the server contained `printf` formatting specifiers, such as `%d` or `%s`
  (gh-8043).
