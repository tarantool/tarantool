## feature/lua/datetime

 * Add a new builtin module `datetime.lua` which allows to operate
   timestamps and intervals values (gh-5941);
 * Parse method to allow converting string literals in extended iso-8601
   or rfc3339 formats (gh-6731);
 * The range of supported years has been extended in all parsers to cover
   fully -5879610-06-22..5879611-07-11 (gh-6731).
