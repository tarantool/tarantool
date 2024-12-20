## bugfix/datetime

* `datetime` methods now handle timezones properly. They now behave like
  offsets for timestamp values and only affect the way the date is
  printed (gh-10363).
