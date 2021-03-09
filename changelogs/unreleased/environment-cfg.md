## feature/core

* Now, it is possible to set box.cfg options with environment variables
  (gh-5602).

  The priority of sources of configuration options is the following (from low
  to high): default, tarantoolctl, environment, box.cfg{}.
