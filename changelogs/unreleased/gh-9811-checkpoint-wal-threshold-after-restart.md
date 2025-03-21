## bugfix/box

* Fixed a bug when the sizes of *.xlog files created before the server restart
  were not taken into account during the `checkpoint_wal_threshold` exceedance
  checks (gh-9811).
