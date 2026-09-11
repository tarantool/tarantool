## bugfix/core

* Fixed a one-byte out-of-bounds write in the in-place log message escaping
  helper (`syslog_escape_inplace()` and the JSON variants) that happened when
  the escaped message filled the log buffer exactly.
