## bugfix/core

* Fixed decoding of escape sequences for single-byte character codes from YAML.
  Before the fix, single-byte character codes between `0x80` and `0xff` would
  be erroneously converted to two-byte UTF-8 code points, for example, `\x80`
  would be decoded as `\uC280` (gh-8782).
