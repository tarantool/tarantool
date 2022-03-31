## feature/lua/datetime

* Added a new builtin module `datetime.lua` that allows to operate timestamps
  and intervals values (gh-5941).

* Added the method to allow converting string literals in extended iso-8601 or
  rfc3339 formats (gh-6731).

* Extended the range of supported years in all parsers to cover fully
  -5879610-06-22..5879611-07-11 (gh-6731).
