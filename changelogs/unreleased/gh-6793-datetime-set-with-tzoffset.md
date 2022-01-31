## bugfix/datetime

 * Fixed a bug in datetime module when `date:set{tzoffset=XXX}` was not
   producing the same result with `date.new{tzoffset=XXX}` for the same
   set of attributes passed (gh-6793).
