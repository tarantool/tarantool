## feature/datetime

* Allowed using human-readable timezone names (for example, 'Europe/Moscow')
  in datetime strings. Use IANA `tzdata` (Olson DB) for timezone-related
  operations, such as DST-based timezone offset calculations (gh-6751).

* The `isdst` field in the datetime object is now calculated correctly, according
  to the IANA `tzdata` (aka Olson DB) rules for the given date/time moment
  (gh-6751).

* The `datetime` module exports the bidirectional `TZ` array, which can be used
  to translate the timezone index (`tzindex`) into timezone names, and
  vice versa (gh-6751).
