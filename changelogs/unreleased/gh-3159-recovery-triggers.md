## feature/box

* It is now possible to register triggers for various recovery stages: use
  `box.ctl.on_recovery_state()` before the initial `box.cfg()` call.
  The trigger has one parameter – a string that shows the reached recovery stage:
  `snapshot_recovered`, `wal_recovered`, `indexes_built`, or `synced` (gh-3159).
