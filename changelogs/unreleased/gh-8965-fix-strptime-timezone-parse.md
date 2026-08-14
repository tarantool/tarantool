## bugfix/datetime

* `strptime()` (and `datetime.parse()` with a custom format) now fails
  on an unknown timezone name in `%Z` instead of silently ignoring it.
  This also fixes a possible stack overflow when parsing a string with
  many `%Z` directives (gh-8965).
