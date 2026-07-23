## feature/box

* Allow setting a custom read-only reason via
  `box.cfg{read_only = true, ro_reason = 'reason'}`. The custom reason is
  reported via `box.info.ro_reason` and in `ER_READONLY` errors (gh-10404).
