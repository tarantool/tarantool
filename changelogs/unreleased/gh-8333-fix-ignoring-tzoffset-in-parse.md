## bugfix/lua/datetime

* Fixed a bug that caused `datetime.parse()` ignore `tzoffset`
  option if a custom format was used (gh-8333).
