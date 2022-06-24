## bugfix/datetime

* Fixed a bug in datetime module when `date:set{hour=nil,min=XXX}`
  did not retain original `hour` value. (gh-7298).
