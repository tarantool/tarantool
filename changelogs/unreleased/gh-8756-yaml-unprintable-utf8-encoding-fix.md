## bugfix/core

* Eliminated implicit conversion of unprintable utf-8 strings to binary blobs
  when encoded in YAML. Now unprintable characters are encoded as escaped utf-8
  code points, for example, `\x80` or `\u200B` (gh-8756).
