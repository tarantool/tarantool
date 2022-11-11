## bugfix/datetime

* Fixed interval arithmetic for boundaries crossing DST (gh-7700).

  Results of datetime arithmetic operations could get a
  different timezone if the DST boundary has been
  crossed during the operation:

  ```
  tarantool> datetime.new{year=2008, month=1, day=1,
                          tz='Europe/Moscow'} +
             datetime.interval.new{month=6}
  ---
  - 2008-07-01T01:00:00 Europe/Moscow
  ...
  ```

  Now we resolve `tzoffset` at the end of operation if
  `tzindex` is not 0.
