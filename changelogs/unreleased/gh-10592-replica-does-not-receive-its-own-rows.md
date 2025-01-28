## bugfix/replication

* Fixed a bug when a master who crashed and lost his xlogs could not get his
  own rows when it tried to re-join the cluster and hangs forever (gh-10592).
