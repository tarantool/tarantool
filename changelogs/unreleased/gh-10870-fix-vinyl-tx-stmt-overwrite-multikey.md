## bugfix/vinyl

* Fixed a bug when a tuple could disappear from a multikey index in case it
  replaced a tuple with duplicate multikey array entries created in the same
  transaction. With the enabled `defer_deletes` space option, the bug could
  also trigger a crash (gh-10869, gh-10870).
