## feature/box

* Errors now carry the offending key in their payload (gh-13818):
  * `FUNC_INDEX_FORMAT`, `FIELD_FOREIGN_KEY_FAILED`,
    `COMPLEX_FOREIGN_KEY_FAILED` and `FOREIGN_KEY_INTEGRITY` have the `key`
    field;
  * `ITERATOR_POSITION` raised for a position that does not fit the index or
    the search criteria has the `position` and `key` fields and the index
    details;
  * the `begin_key must be < end_key` error of `index:delete_range()` and
    `index:quantile()` has the `begin_key` and `end_key` fields.
