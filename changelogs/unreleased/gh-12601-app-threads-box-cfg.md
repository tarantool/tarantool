## feature/core

* Now, if Tarantool is started without a configuration file and
  `box.cfg.app_threads` is set from application code, the
  `experimental.threads` module is initialized with the default
  thread group named `app` (gh-12601).
