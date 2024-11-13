## bugfix/vinyl

* Added a fiber slice check to `index.count()` to prevent it from blocking
  for too long while counting tuples in a space stored in memory (gh-10553).
