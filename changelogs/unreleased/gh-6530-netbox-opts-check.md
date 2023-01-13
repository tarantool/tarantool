## bugfix/core

* Added type checking for options in `net.box` remote queries and
  `connect` method. Now graceful errors are thrown in case of incorrect
  options (gh-6063, gh-6530).
