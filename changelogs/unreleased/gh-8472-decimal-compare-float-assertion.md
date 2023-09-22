## bugfix/core

* Fixed a wrong assertion in index comparators when comparing decimals with
  floats greater than `1e38`. The error was present only in the debug build.
  Despite the failing assertion, the behavior after the assertion was correct
  (gh-8472).
