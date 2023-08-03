## bugfix/core

* Fixed a bug when a space that is referenced by a foreign key could not
  be truncated even if the referring space was empty (gh-8946).
