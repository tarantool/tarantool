## bugfix/sql

* Fixed undefined behavior of SQL when using positive `MP_INT` numbers in
  MessagePack anywhere (bound arguments, tuples, C functions) (gh-10132).
