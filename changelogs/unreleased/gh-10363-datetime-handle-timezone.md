## bugfix/datetime

* Proper behavior of applying timezones to datetime objects can
  now be enabled by setting configuration option
  `compat.datetime_apply_timezone_action` to `'new'`. It makes
  changing timezones of `datetime` objects preserve the represented
  timestamp and affect the represented time of day (gh-10363).
