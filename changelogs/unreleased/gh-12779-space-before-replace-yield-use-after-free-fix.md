## bugfix/core

* Fixed a heap-use-after-free bug when a `space.before_replace` trigger yielded
  with disabled MVCC (gh-12779).
