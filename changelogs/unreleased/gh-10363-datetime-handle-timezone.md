## bugfix/datetime

* `datetime` methods now handle timezones properly. Now timezone changes
  only affect the way the object is formatted and don't alter the
  represented timestamp (gh-10363).
