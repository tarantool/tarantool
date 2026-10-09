## bugfix/vinyl

* Fixed an out-of-bounds read when reading a page from a malformed vinyl
  run file: the row index offset and the per-statement row index values
  are now validated against the page size on decode, instead of being
  used unchecked to slice into the page buffer.
