## bugfix/core

* Now foreign keys from non-temporary to temporary and from non-local to local
  spaces are prohibited since they can potentially break foreign key consistency
  (gh-8936).
