## bugfix/sql

* Fixed a bug where the `box.execute()` result metadata reported the `any`
  type instead of `boolean` for a `>=` expression (gh-13358).
