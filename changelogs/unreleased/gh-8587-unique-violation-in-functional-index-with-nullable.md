## bugfix/core

* Fixed a bug when a tuple could be inserted even if it violates a `unique`
  constraint of a functional index that has a nullable part (gh-8587).
