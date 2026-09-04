## bugfix/sql

* Fixed a bug where an unquoted SQL identifier whose uppercase form is longer
  than the original (for example, some Unicode letters) produced an
  unterminated name over partly uninitialized memory.
