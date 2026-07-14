## feature/box

* Allow setting custom read-only details via
  `box.cfg{read_only = true, ro_details = 'maintenance'}`. The text is reported
  via `box.info.ro_details` and the `details` field of `ER_READONLY` errors.
  It is for human consumption and must not be interpreted programmatically.
  Existing `ro_reason` and error `reason` values are unchanged (gh-10404).
