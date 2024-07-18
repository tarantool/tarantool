## bugfix/box

* Now fully-temporary spaces DDL does not abort concurrent purely remote
  (applier) transactions, and DDL in purely remote transactions does not abort
  concurrent fully-temporary spaces transactions (gh-9720).
