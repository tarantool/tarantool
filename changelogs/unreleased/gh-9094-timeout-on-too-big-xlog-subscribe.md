## bugfix/replication

* Fixed a bug when a replica could timeout on subscribe if the master had to
  open a big enough xlog file for that (gh-9094).
