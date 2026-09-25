## feature/sql

* Added a new option sql_uppercased_id. When the value is `new` we do search
  in sql looks only for an exact name match. When the value is `old` we
  additionally do search in sql first looks for an exact match; if that fails,
  we do searc using the name converted to uppercase. The default setting is
  `old`. If name in request enclosed in quotation marks we use `new` behaviour.
  This can be toggled using the command `compat.sql_legacy_name_normalization = option`
  (gh-12732).
