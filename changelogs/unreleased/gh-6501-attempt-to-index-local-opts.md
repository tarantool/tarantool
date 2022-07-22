## bugfix/box

* Added the check of the iterator type in the `select`, `count`, and `pairs` methods of
  the index object. Iterator can now be passed to these methods directly: `box.index.ALL`, `box.index.GT`,
  and so on (gh-6501).
