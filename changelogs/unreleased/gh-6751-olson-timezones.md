## feature/datetime

 * Allow to use human-readable timezone names (e.g. 'Europe/Moscow')
   in datetime strings. Use IANA tzdata (aka Olson DB) for timezone related
   operations, such as DST-based timezone offset calculations (gh-6751);
 * `isdst` field in datetime object is now properly calculated according to
   IANA tzdata (aka Olson DB) rules for given date/time moment (gh-6751);
 * `datetime` module exports bidirectional `TZ` array, which may be used
   for translation of timezone index (`tzindex`) to timezone names, and in
   reverse (gh-6751).
