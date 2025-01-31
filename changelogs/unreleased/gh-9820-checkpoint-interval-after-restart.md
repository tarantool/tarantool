## bugfix/box

* Fixed a bug when the timestamps of snapshots created before the server restart
  were not taken into account with `checkpoint_interval` enabled (gh-9820).
