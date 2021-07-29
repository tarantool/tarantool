## feature/sql

* SQL built-in functions were removed from \_func system space (gh-6106).
* Function are now looked up first in SQL built-in functions and then in
  user-defined functions.
* Fixed incorrect error message in case of misuse of the function used to set
  the default value.
