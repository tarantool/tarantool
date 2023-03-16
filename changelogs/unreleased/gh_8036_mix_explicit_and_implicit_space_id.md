## bugfix/core

* Fixed a problem when space creation failed with duplication error when
  explicit and implicit space id were mixed. Now, actual maximal space id
  is used to generate a new one (gh-8036).
