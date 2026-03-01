## feature/core

* Added support for the XDG Base Directory specification for console history
  file. The history file is now searched in `$XDG_STATE_HOME/tarantool` and
  `$HOME/.local/state/tarantool` directories, with the legacy
  `$HOME/.tarantool_history` location taking priority for seamless migration
  (gh-12356).
