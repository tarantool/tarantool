## bugfix/config

* Now Tarantool writes a detailed error message if it finds
  replica sets with the same names in different groups or instances
  with the same names in different replica sets in the provided
  configuration (gh-10347).
