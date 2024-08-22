## bugfix/vinyl

* Eliminated an unnecessary disk read when a key that was recently updated or
  deleted was accessed via a unique secondary index (gh-10442).
