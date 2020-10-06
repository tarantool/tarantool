## bugfix/lua

* Fixed a leak of a tuple object in `key_def:compare_with_key(tuple, key)`,
  when serialization of the key fails (gh-5388).
