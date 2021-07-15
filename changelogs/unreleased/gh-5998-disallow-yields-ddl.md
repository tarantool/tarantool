## bugfix/core

* Disallow yields after DDL operations in MVCC mode. It fixes crash which takes
  place in case several transactions refer to system spaces (gh-5998).
