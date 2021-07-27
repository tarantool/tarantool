## feature/sql

* Now a numeric value can be cast to another numeric type only if the cast is
  precise. In addition, a UUID value cannot be implicitly cast to
  STRING/VARBINARY, and a STRING/VARBINARY value cannot be implicitly cast to
  a UUID (gh-4470).
