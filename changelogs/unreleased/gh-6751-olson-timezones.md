## feature/datetime

 * `isdst` field in datetime object is now properly calculated according to
   IANA tzdata (aka Olson DB) rules for given date/time moment (gh-6751);
