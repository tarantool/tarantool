## bugfix/core

- Fixed a bug in the numbers comparison and hashing in tuple keys. It allowed to
  insert the same key multiple times into a unique index, and sometimes wouldn't
  allow to find an existing key in an index. Could happen when numbers were
  encoded in MessagePack suboptimally (gh-9965).
