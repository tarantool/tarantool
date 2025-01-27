## bugfix/core

- Fixed several bugs in the numbers comparison and hashing in tuple keys. It
  allowed to insert the same key multiple times into a unique index, and
  sometimes wouldn't allow to find an existing key in an index. Could happen
  when numbers were encoded in MessagePack suboptimally and when `double` field
  type was used (gh-9965).

----
Users who ever used `double` field type in vinyl indexes might have their
indexes broken. Regardless of this release. Preferably prior to the upgrade the
users must get rid of the `double` indexed field type in vinyl. It must be
drop-in replaceable by the `number` field type.

The reason is that the sorting of `double` fields would compare the field values
as C-language doubles always, even when actual integers are stored. This works
fine for small numbers, but starting from 2^53 the double-style comparison
looses precision. For example, 18446744073709551615 and 18446744073709551614
would be considered the same values.

Besides, point-lookups in such index are going to be broken by design too -
looking up by 18446744073709551615 can return 18446744073709551614 and many
other integers around this point.

Unfortunately, even if the user doesn't have `double` type in any vinyl indexes,
but **ever had it before**, it is still unsafe. `double` index altered to
`number` or `scalar` would remain broken. It is **very important that the users
rebuild all the vinyl indexes that have or ever had `double` field type in
them**. What is worse, just an alter `double` -> `number` **won't work**. The
old index must be dropped and a new one must be created. In any order.
