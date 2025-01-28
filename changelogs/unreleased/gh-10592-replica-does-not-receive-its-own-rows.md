## bugfix/replication

* Fixed a bug when a master that crashed and lost its xlogs could not get his
  own rows when it tried to re-join the cluster and hanged forever (gh-10592).
