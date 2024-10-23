## feature/connpool

* Now `connpool.filter()` returns only alive instances by default.
  To use the old behaviour and acquire all instances the new option
  `skip_connection_check` can be used (gh-10596).
