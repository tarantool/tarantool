## bugfix/box

* Fixed the issue with hanging write operations forever triggered by heavy
  write load and WAL writing failures on cascade rollback (gh-11081).
