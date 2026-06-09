## feature/box

* `box.backup.start()` has been enhanced to back up xlogs that are rotated at
  the time of the call (gh-11729).
* Added the `from_vclock` parameter to the `box.backup.start()` to enable
  incremental backups (gh-11729).
* Added the `ttl` parameter to the `box.backup.start()` to set expiration time
  for backups (gh-11729).
* Added the `box_backup_default_ttl` compatibility option to control the
  default expiration time for backups (gh-11729).
* Added a new API, `box.backup.info()` to retrieve information about running
  backups (gh-11729).
