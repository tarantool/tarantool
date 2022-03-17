## bugfix/core

* Improved incoming tuple validation. Now tuples coming over the net can't
  contain malformed decimals, uuids, datetime values (gh-6857).
