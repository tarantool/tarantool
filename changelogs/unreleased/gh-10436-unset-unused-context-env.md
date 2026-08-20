## bugfix/config

* Unused `config.context` variables that cannot be read (an unset
  environment variable or an unreadable file) no longer prevent an
  instance from starting. A warning is logged for each such variable
  (gh-10436).
