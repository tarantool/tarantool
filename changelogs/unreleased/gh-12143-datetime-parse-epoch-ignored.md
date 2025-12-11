## bugfix/datetime

* The epoch seconds value parsed with `'%s'`
  by `datetime.parse()` isn't ignored and used
  now as a timestamp value by `datetime.new()` (gh-12143).
