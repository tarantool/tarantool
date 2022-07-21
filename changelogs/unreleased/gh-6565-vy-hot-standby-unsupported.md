## bugfix/vinyl

* Explicitly disabled the hot standby mode for vinyl. Now an attempt to enable
  the hot standby mode in case the master instance has vinyl spaces results in
  an error. Before this change, the behavior was undefined (gh-6565).
