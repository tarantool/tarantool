## bugfix/datetime

* Fixed errors on creating dates with historically valid tzoffset
  values, which is out of modern standard range of [-12:00, +14:00]
  (gh-12417).
