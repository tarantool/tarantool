## bugfix/recovery

* When `force_recovery` cfg option is set, Tarantool is able to boot from
  `snap`/`xlog` combinations where `xlog` covers changes committed both before
  and after `snap` creation. For example, `0...0.xlog`, covering everything up
  to vclock `{1: 15}` and `0...09.snap`, corresponding to `vclock `{1: 9}` (gh-6794).
