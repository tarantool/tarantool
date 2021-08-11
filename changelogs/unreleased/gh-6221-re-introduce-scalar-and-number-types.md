## feature/sql

* The SCALAR and NUMBER types have been reworked in SQL. Now SCALAR values
cannot be implicitly cast to any other scalar type, and NUMBER values cannot be
implicitly cast to any other numeric type. This means that arithmetic and
bitwise operations and concatenation are no longer allowed for SCALAR and NUMBER
values. In addition, any SCALAR value can now be compared with values of any
other scalar type using the SCALAR rules (gh-6221).
