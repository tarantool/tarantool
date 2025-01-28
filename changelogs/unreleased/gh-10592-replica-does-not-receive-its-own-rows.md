## bugfix/recovery

* Fixed a bug where a master node that crashed and lost its xlog files for
  some reason might never get some of its own rows from upstreams after
  reconnecting. A new ro-reason "waiting_for_own_rows" was introduced for this.
  Now, until the instance has received all its rows, it is in this mode and
  remains read only (gh-10592).
