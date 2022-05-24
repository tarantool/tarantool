## feature/datetime

* Changed format of a string representation of date/time intervals.
  There is no seconds/nano-seconds normalization done anymore, and
  interval to be displayed as-is.

  Instead of former:
```lua
    local ival = date.interval.new{year = 12345, hour = 48, min = 3, sec = 1,
                                   nsec = 12345678}
    print(tostring(ival)) -- '+12345 years, 48 hours, 3 minutes, '..
                          -- '1.012345678 seconds'
```
we now output
```
   --  '+12345 years, 48 hours, 3 minutes, 1 seconds, 12345678 nanoseconds'
```
