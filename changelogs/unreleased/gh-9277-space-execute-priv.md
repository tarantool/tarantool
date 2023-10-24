## bugfix/box

* **[Breaking change]** `box.schema.user.grant()` now raises an error on
  an attempt to grant the `execute` privilege on a space.  Historically,
  this action was allowed although it had no effect. It's still possible
  to revert to the old behavior with the new compatibility option
  `box_space_execute_priv` (gh-9277).
