## bugfix/datetime

* Fixed a bug in datetime module when `date:set{tzoffset=XXX}` did not
  produce the same result with `date.new{tzoffset=XXX}` for the same
  set of attributes passed (gh-6793).
