## bugfix/vinyl

* Fixed a bug when recovery could fail with the error "Invalid VYLOG file:
  Deleted range XXXX has run slices" or "Invalid VYLOG file: Run XXXX committed
  after deletion" after an index drop (gh-10277).
