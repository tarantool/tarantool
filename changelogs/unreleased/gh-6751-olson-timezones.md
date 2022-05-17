## feature/datetime

 * `isdst` field in datetime object is now properly calculated according to
   IANA tzdata (aka Olson DB) rules for given date/time moment (gh-6751);
 * `datetime` module exports bidirectional `TZ` array, which may be used
   for translation of timezone index (`tzindex`) to timezone names, and in
   reverse (gh-6751);
