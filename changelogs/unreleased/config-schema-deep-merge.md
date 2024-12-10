## bugfix/config

* `<schema object>:merge()` now performs a deep merge inside an `any` scalar
  value if left-hand and right-hand values are both tables, where all the keys
  are strings. This way the cluster configuration options that are marked as
  `any` in the schema (fields of `app.cfg` and `roles_cfg`) are merged deeply
  (gh-10450).
