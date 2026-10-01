## feature/sql

* Added a new option `sql_legacy_name_normalization`. When the value is
  `new`, the search in SQL looks only for an exact name match. When the
  value is `old`, the search in SQL first looks for an exact match; if
  that fails, it searches using the name converted to uppercase. The
  default setting is `old`. If the name in the request is enclosed in
  quotation marks, we use the `new` behaviour (gh-12732).
