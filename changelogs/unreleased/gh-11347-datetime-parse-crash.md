## bugfix/datetime

* Fixed an ambiguous case where day of year (`yday`, which defines
  calendar month and month day implicitly) and calendar month
  (without a month day) were both defined in the date text and that lead
  to error instead of crash with assertion failure (gh-11347).
