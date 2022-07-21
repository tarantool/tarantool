## bugfix/core

* Improved validation of incoming tuples. Now tuples coming over the network
 can't contain malformed decimals, uuids, or datetime values (gh-6857).
