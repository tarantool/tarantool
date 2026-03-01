## feature/core

* Added support for the XDG Base Directory specification for the console history
  file. The history file is now searched for in `$XDG_STATE_HOME/tarantool` and
  `$HOME/.local/state/tarantool` directories, with the legacy
  `$HOME/.tarantool_history` location taking priority to ensure seamless migration.
  On fresh installations, the XDG-compliant state directory is created
  automatically (gh-12356).
