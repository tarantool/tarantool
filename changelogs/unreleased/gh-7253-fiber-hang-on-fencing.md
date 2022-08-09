## bugfix/replication

* Fixed a bug when a fiber committing a synchronous transaction could hang if
  the instance got a term bump during that or its synchro-queue was fenced in
  any other way (gh-7253).
