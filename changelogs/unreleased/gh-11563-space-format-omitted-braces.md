## bugfix/box

* `space:format()` no longer silently resets the format to empty when a
  single field definition is passed without wrapping braces; it is now
  normalized the same way `create_index`'s `parts` option is (gh-11563).
