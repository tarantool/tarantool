## bugfix/sql

* Fixed undefined behaviour of SQL when using positive `MP_INT` numbers in
  MessagePack anywhere (bound arguments, tuples, C functions) (gh-10132).
