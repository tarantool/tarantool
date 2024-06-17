## bugfix/core

* Fixed the bug that on Mac the system setting `kern.ipc.somaxconn` was ignored
  for listening sockets. Now it is used, but capped at 32367 due to how
  `listen()` works on Mac (gh-8130).
