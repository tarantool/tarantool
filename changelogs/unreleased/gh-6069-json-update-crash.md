## bugfix/core

* Fixed a crash in JSON update on tuple/space when it had more than one
  operation, they accessed fields in reversed order, and these fields didn't
  exist. Example: `box.tuple.new({1}):update({{'=', 4, 4}, {'=', 3, 3}})`
  (gh-6069).
