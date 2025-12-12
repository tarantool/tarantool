## bugfix/datetime

* Ambiguous case where day of year (yday, which is defines
  calendar month and month day implicitly) and calendar month
  (without a month day) are both defined in the date text lead
  to error instead of crash with assertion fail (gh-11347).
