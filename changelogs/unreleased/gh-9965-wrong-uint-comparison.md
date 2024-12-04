## bugfix/core

- Fixed a bug in the unsigned numbers comparison in tuple keys. It allowed to
  insert the same key multiple times into a unique index, and sometimes wouldn't
  allow to find an existing key in an index (gh-9965).
