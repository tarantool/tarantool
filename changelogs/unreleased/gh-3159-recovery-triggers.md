## feature/box

* It is now possible to register triggers for various recovery stages: use
  `box.ctl.on_recovery_state()` before initial `box.cfg()` call. The trigger
  receives one parameter - a string resembling reached recovery stage. Possible
  values are "snapshot_recovered", "wal_recovered", "indexes_built", "synced"
  (gh-3159).
