## bugfix/datetime

* Fixed an out-of-bounds read on a datetime value whose timezone index equals
  the timezone table size. Such an index is now rejected while the value is
  decoded.
