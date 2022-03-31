## bugfix/core

* Disallowed yields after DDL operations in MVCC mode. It fixes a crash which
  takes place in case several transactions refer to system spaces (gh-5998).
