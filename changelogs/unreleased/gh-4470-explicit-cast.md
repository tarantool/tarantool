## feature/sql

* Removed explicit cast from BOOLEAN to numeric types and vice versa (gh-4770).
* Removed explicit cast from VARBINARY to numeric types and vice versa (gh-4772,
  gh-5852).
* Fixed a bug due to which a string that is not NULL terminated could not be
  cast to BOOLEAN, even if the conversion should be successful according to the
  rules.
